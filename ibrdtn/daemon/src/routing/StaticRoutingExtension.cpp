/*
 * StaticRoutingExtension.cpp
 *
 *  Created on: 16.02.2010
 *      Author: morgenro
 */

#include "Configuration.h"
#include "routing/StaticRoutingExtension.h"
#include "routing/QueueBundleEvent.h"
#include "routing/StaticRouteChangeEvent.h"
#include "net/TransferAbortedEvent.h"
#include "net/TransferCompletedEvent.h"
#include "net/ConnectionEvent.h"
#include "routing/RequeueBundleEvent.h"
#include "core/NodeEvent.h"
#include "storage/SimpleBundleStorage.h"
#include "core/TimeEvent.h"

#include <ibrdtn/utils/Clock.h>

#include <ibrcommon/Logger.h>
#include <ibrcommon/thread/MutexLock.h>

#include <typeinfo>
#include <memory>

namespace dtn
{
	namespace routing
	{
		StaticRoutingExtension::StaticRoutingExtension()
		 : next_expire(0)
		{
		}

		StaticRoutingExtension::~StaticRoutingExtension()
		{
			stop();
			join();

			// delete all static routes
			for (std::list<StaticRoute*>::iterator iter = _routes.begin();
					iter != _routes.end(); iter++)
			{
				StaticRoute *route = (*iter);
				delete route;
			}
		}

		void StaticRoutingExtension::__cancellation()
		{
			_taskqueue.abort();
		}

		void StaticRoutingExtension::run()
		{
			class BundleFilter : public dtn::storage::BundleStorage::BundleFilterCallback
			{
			public:
				BundleFilter(const NeighborDatabase::NeighborEntry &entry, const std::list<const StaticRoute*> &routes)
				 : _entry(entry), _routes(routes)
				{};

				virtual ~BundleFilter() {};

				virtual size_t limit() const { return 10; };

				virtual bool shouldAdd(const dtn::data::MetaBundle &meta) const
				{
					// check Scope Control Block - do not forward bundles with hop limit == 0
					if (meta.hopcount == 0)
					{
						return false;
					}

					// do not forward any routing control message
					// this is done by the neighbor routing module
					if (isRouting(meta.source))
					{
						return false;
					}

					// do not forward local bundles
					if ((meta.destination.getNode() == dtn::core::BundleCore::local)
							&& meta.get(dtn::data::PrimaryBlock::DESTINATION_IS_SINGLETON)
						)
					{
						return false;
					}

					// check Scope Control Block - do not forward non-group bundles with hop limit <= 1
					if ((meta.hopcount <= 1) && (meta.get(dtn::data::PrimaryBlock::DESTINATION_IS_SINGLETON)))
					{
						return false;
					}

					// do not forward to any blacklisted destination
					const dtn::data::EID dest = meta.destination.getNode();
					if (_blacklist.find(dest) != _blacklist.end())
					{
						return false;
					}

					// do not forward bundles already known by the destination
					if (_entry.has(meta))
					{
						return false;
					}

					// search for one rule that match
					for (std::list<const StaticRoute*>::const_iterator iter = _routes.begin();
							iter != _routes.end(); iter++)
					{
						const StaticRoute &route = (**iter);

						if (route.match(meta.destination))
						{
							return true;
						}
					}

					return false;
				};

				void blacklist(const dtn::data::EID& id)
				{
					_blacklist.insert(id);
				};

			private:
				std::set<dtn::data::EID> _blacklist;
				const NeighborDatabase::NeighborEntry &_entry;
				const std::list<const StaticRoute*> _routes;
			};

			// announce static routes here
			const std::multimap<std::string, std::string> &routes = dtn::daemon::Configuration::getInstance().getNetwork().getStaticRoutes();

			for (std::multimap<std::string, std::string>::const_iterator iter = routes.begin(); iter != routes.end(); iter++)
			{
				const dtn::data::EID nexthop((*iter).second);
				dtn::routing::StaticRouteChangeEvent::raiseEvent(dtn::routing::StaticRouteChangeEvent::ROUTE_ADD, nexthop, (*iter).first);
			}

			dtn::storage::BundleStorage &storage = (**this).getStorage();

			while (true)
			{
				NeighborDatabase &db = (**this).getNeighborDB();

				try {
					Task *t = _taskqueue.getnpop(true);
					std::auto_ptr<Task> killer(t);

					IBRCOMMON_LOGGER_DEBUG(5) << "processing static routing task " << t->toString() << IBRCOMMON_LOGGER_ENDL;

					try {
						SearchNextBundleTask &task = dynamic_cast<SearchNextBundleTask&>(*t);

						std::list<const StaticRoute*> routes;

						// look for routes to this node
						for (std::list<StaticRoute*>::const_iterator iter = _routes.begin();
								iter != _routes.end(); iter++)
						{
							const StaticRoute *route = (*iter);
							if (route->getDestination() == task.eid)
							{
								// add to the valid routes
								routes.push_back(route);
							}
						}

						if (routes.size() > 0)
						{
							// this destination is not handles by any static route
							ibrcommon::MutexLock l(db);
							NeighborDatabase::NeighborEntry &entry = db.get(task.eid);

							// get the bundle filter of the neighbor
							BundleFilter filter(entry, routes);

							// some debug
							IBRCOMMON_LOGGER_DEBUG(40) << "search some bundles not known by " << task.eid.getString() << IBRCOMMON_LOGGER_ENDL;

							// blacklist the neighbor itself, because this is handled by neighbor routing extension
							filter.blacklist(task.eid);

							// query all bundles from the storage
							const std::list<dtn::data::MetaBundle> list = storage.get(filter);

							// send the bundles as long as we have resources
							for (std::list<dtn::data::MetaBundle>::const_iterator iter = list.begin(); iter != list.end(); iter++)
							{
								try {
									// transfer the bundle to the neighbor
									transferTo(entry, *iter);
								} catch (const NeighborDatabase::AlreadyInTransitException&) { };
							}
						}
					} catch (const NeighborDatabase::NoMoreTransfersAvailable&) {
					} catch (const NeighborDatabase::NeighborNotAvailableException&) {
					} catch (const std::bad_cast&) { };

					try {
						const ProcessBundleTask &task = dynamic_cast<ProcessBundleTask&>(*t);
						IBRCOMMON_LOGGER_DEBUG(50) << "search static route for " << task.bundle.toString() << IBRCOMMON_LOGGER_ENDL;

						// look for routes to this node
						for (std::list<StaticRoutingExtension::StaticRoute*>::const_iterator iter = _routes.begin();
								iter != _routes.end(); iter++)
						{
							const StaticRoutingExtension::StaticRoute &route = (**iter);
							IBRCOMMON_LOGGER_DEBUG(50) << "check static route: " << route.toString() << IBRCOMMON_LOGGER_ENDL;
							try {
								if (route.match(task.bundle.destination))
								{
									ibrcommon::MutexLock l(db);
									NeighborDatabase::NeighborEntry &n = db.get(route.getDestination());

									// transfer the bundle to the neighbor
									transferTo(n, task.bundle);
								}
							} catch (const NeighborDatabase::NeighborNotAvailableException&) {
								// neighbor is not available, can not forward this bundle
							} catch (const NeighborDatabase::NoMoreTransfersAvailable&) {
							} catch (const NeighborDatabase::AlreadyInTransitException&) { };
						}
					} catch (const std::bad_cast&) { };

					try {
						const RouteChangeTask &task = dynamic_cast<RouteChangeTask&>(*t);

						// delete all similar routes
						for (std::list<StaticRoute*>::iterator iter = _routes.begin();
								iter != _routes.end();)
						{
							StaticRoute *route = (*iter);
							if (route->toString() == task.route->toString())
							{
								delete route;
								_routes.erase(iter++);
							}
							else
							{
								iter++;
							}
						}

						if (task.type == RouteChangeTask::ROUTE_ADD)
						{
							_routes.push_back(task.route);
							_taskqueue.push( new SearchNextBundleTask(task.route->getDestination()) );

							ibrcommon::MutexLock l(_expire_lock);
							if (next_expire > task.route->getExpiration())
							{
								next_expire = task.route->getExpiration();
							}
						}
						else
						{
							delete task.route;

							// force a expiration process
							ibrcommon::MutexLock l(_expire_lock);
							next_expire = 1;
						}
					} catch (const bad_cast&) { };

					try {
						dynamic_cast<ClearRoutesTask&>(*t);

						// delete all static routes
						for (std::list<StaticRoute*>::iterator iter = _routes.begin();
								iter != _routes.end(); iter++)
						{
							StaticRoute *route = (*iter);
							delete route;
						}
						_routes.clear();

						ibrcommon::MutexLock l(_expire_lock);
						next_expire = 0;
					} catch (const bad_cast&) { };

					try {
						const ExpireTask &task = dynamic_cast<ExpireTask&>(*t);

						ibrcommon::MutexLock l(_expire_lock);
						next_expire = 0;

						// search for expired items
						for (std::list<StaticRoute*>::iterator iter = _routes.begin();
								iter != _routes.end();)
						{
							StaticRoute *route = (*iter);

							if (route->getExpiration() < task.timestamp)
							{
								delete route;
								_routes.erase(iter++);
							}
							else
							{
								if ((next_expire == 0) || (next_expire > route->getExpiration()))
								{
									next_expire = route->getExpiration();
								}

								iter++;
							}
						}
					} catch (const bad_cast&) { };

				} catch (const std::exception &ex) {
					IBRCOMMON_LOGGER_DEBUG(20) << "static routing failed: " << ex.what() << IBRCOMMON_LOGGER_ENDL;
					return;
				}

				yield();
			}
		}

		void StaticRoutingExtension::notify(const dtn::core::Event *evt)
		{
			try {
				const QueueBundleEvent &queued = dynamic_cast<const QueueBundleEvent&>(*evt);
				_taskqueue.push( new ProcessBundleTask(queued.bundle, queued.origin) );
				return;
			} catch (const std::bad_cast&) { };

			try {
				const dtn::core::NodeEvent &nodeevent = dynamic_cast<const dtn::core::NodeEvent&>(*evt);
				const dtn::core::Node &n = nodeevent.getNode();

				if (nodeevent.getAction() == NODE_AVAILABLE)
				{
					_taskqueue.push( new SearchNextBundleTask(n.getEID()) );
				}
				return;
			} catch (const std::bad_cast&) { };

			try {
				const dtn::net::ConnectionEvent &ce = dynamic_cast<const dtn::net::ConnectionEvent&>(*evt);

				if (ce.state == dtn::net::ConnectionEvent::CONNECTION_UP)
				{
					// send all (multi-hop) bundles in the storage to the neighbor
					_taskqueue.push( new SearchNextBundleTask(ce.peer) );
				}
				return;
			} catch (const std::bad_cast&) { };

			// The bundle transfer has been aborted
			try {
				const dtn::net::TransferAbortedEvent &aborted = dynamic_cast<const dtn::net::TransferAbortedEvent&>(*evt);
				_taskqueue.push( new SearchNextBundleTask(aborted.getPeer()) );
				return;
			} catch (const std::bad_cast&) { };

			// A bundle transfer was successful
			try {
				const dtn::net::TransferCompletedEvent &completed = dynamic_cast<const dtn::net::TransferCompletedEvent&>(*evt);
				_taskqueue.push( new SearchNextBundleTask(completed.getPeer()) );
				return;
			} catch (const std::bad_cast&) { };

			// each second, look for expired routes
			try {
				const dtn::core::TimeEvent &time = dynamic_cast<const dtn::core::TimeEvent&>(*evt);

				ibrcommon::MutexLock l(_expire_lock);
				if ((next_expire != 0) && (next_expire < time.getUnixTimestamp()))
				{
					_taskqueue.push( new ExpireTask( time.getUnixTimestamp() ) );
				}
				return;
			} catch (const bad_cast&) { };

			// on route change, generate a task
			try {
				const dtn::routing::StaticRouteChangeEvent &route = dynamic_cast<const dtn::routing::StaticRouteChangeEvent&>(*evt);

				if (route.type == dtn::routing::StaticRouteChangeEvent::ROUTE_CLEAR)
				{
					_taskqueue.push( new ClearRoutesTask() );
					return;
				}

				StaticRoute *r = NULL;

				if (route.pattern.length() > 0)
				{
					r = new RegexRoute(route.pattern, route.nexthop);
				}
				else
				{
					size_t et = dtn::utils::Clock::getUnixTimestamp() + route.timeout;
					r = new EIDRoute(route.destination, route.nexthop, et);
				}

				switch (route.type)
				{
				case dtn::routing::StaticRouteChangeEvent::ROUTE_ADD:
					_taskqueue.push( new RouteChangeTask( RouteChangeTask::ROUTE_ADD, r ) );
					break;

				case dtn::routing::StaticRouteChangeEvent::ROUTE_DEL:
					_taskqueue.push( new RouteChangeTask( RouteChangeTask::ROUTE_DEL, r ) );
					break;

				default:
					break;
				}

				return;
			} catch (const bad_cast&) { };
		}

		// virtual destructor
		StaticRoutingExtension::StaticRoute::~StaticRoute() {};

		StaticRoutingExtension::RegexRoute::RegexRoute(const std::string &regex, const dtn::data::EID &dest)
			: _dest(dest), _regex_str(regex), _invalid(false)
		{
			if ( regcomp(&_regex, regex.c_str(), 0) )
			{
				IBRCOMMON_LOGGER(error) << "Could not compile regex: " << regex << IBRCOMMON_LOGGER_ENDL;
				_invalid = true;
			}
		}

		StaticRoutingExtension::RegexRoute::~RegexRoute()
		{
			if (!_invalid)
				regfree(&_regex);
		}

		StaticRoutingExtension::RegexRoute::RegexRoute(const StaticRoutingExtension::RegexRoute &obj)
			: _dest(obj._dest), _regex_str(obj._regex_str), _invalid(obj._invalid)
		{
			if ( regcomp(&_regex, _regex_str.c_str(), 0) )
			{
				_invalid = true;
			}
		}

		StaticRoutingExtension::RegexRoute& StaticRoutingExtension::RegexRoute::operator=(const StaticRoutingExtension::RegexRoute &obj)
		{
			if (!_invalid)
			{
				regfree(&_regex);
			}

			_dest = obj._dest;
			_regex_str = obj._regex_str;
			_invalid = obj._invalid;

			if (!_invalid)
			{
				if ( regcomp(&_regex, obj._regex_str.c_str(), 0) )
				{
					IBRCOMMON_LOGGER(error) << "Could not compile regex: " << _regex_str << IBRCOMMON_LOGGER_ENDL;
					_invalid = true;
				}
			}

			return *this;
		}

		bool StaticRoutingExtension::RegexRoute::match(const dtn::data::EID &eid) const
		{
			if (_invalid) return false;

			const std::string dest = eid.getString();

			// test against the regular expression
			int reti = regexec(&_regex, dest.c_str(), 0, NULL, 0);

			if( !reti )
			{
				// the expression match
				return true;
			}
			else if( reti == REG_NOMATCH )
			{
				// the expression not match
				return false;
			}
			else
			{
				char msgbuf[100];
				regerror(reti, &_regex, msgbuf, sizeof(msgbuf));
				IBRCOMMON_LOGGER(error) << "Regex match failed: " << std::string(msgbuf) << IBRCOMMON_LOGGER_ENDL;
				return false;
			}
		}

		const dtn::data::EID& StaticRoutingExtension::RegexRoute::getDestination() const
		{
			return _dest;
		}

		const std::string StaticRoutingExtension::RegexRoute::toString() const
		{
			std::stringstream ss;
			ss << _regex_str << " => " << _dest.getString();
			return ss.str();
		}

		StaticRoutingExtension::EIDRoute::EIDRoute(const dtn::data::EID &match, const dtn::data::EID &nexthop, size_t et)
		 : _nexthop(nexthop), _match(match), expiretime(et)
		{
		}

		StaticRoutingExtension::EIDRoute::~EIDRoute()
		{
		}

		bool StaticRoutingExtension::EIDRoute::match(const dtn::data::EID &eid) const
		{
			return (_match == eid.getNode());
		}

		const dtn::data::EID& StaticRoutingExtension::EIDRoute::getDestination() const
		{
			return _nexthop;
		}

		/**
		 * Describe this route as a one-line-string.
		 * @return
		 */
		const std::string StaticRoutingExtension::EIDRoute::toString() const
		{
			std::stringstream ss;
			ss << _match.getString() << " => " << _nexthop.getString();
			return ss.str();
		}

		size_t StaticRoutingExtension::EIDRoute::getExpiration() const
		{
			return expiretime;
		}

		/****************************************/

		StaticRoutingExtension::SearchNextBundleTask::SearchNextBundleTask(const dtn::data::EID &e)
		 : eid(e)
		{ }

		StaticRoutingExtension::SearchNextBundleTask::~SearchNextBundleTask()
		{ }

		std::string StaticRoutingExtension::SearchNextBundleTask::toString()
		{
			return "SearchNextBundleTask: " + eid.getString();
		}

		/****************************************/

		StaticRoutingExtension::ProcessBundleTask::ProcessBundleTask(const dtn::data::MetaBundle &meta, const dtn::data::EID &o)
		 : bundle(meta), origin(o)
		{ }

		StaticRoutingExtension::ProcessBundleTask::~ProcessBundleTask()
		{ }

		std::string StaticRoutingExtension::ProcessBundleTask::toString()
		{
			return "ProcessBundleTask: " + bundle.toString();
		}

		/****************************************/

		StaticRoutingExtension::ClearRoutesTask::ClearRoutesTask()
		{
		}

		StaticRoutingExtension::ClearRoutesTask::~ClearRoutesTask()
		{
		}

		std::string StaticRoutingExtension::ClearRoutesTask::toString()
		{
			return "ClearRoutesTask";
		}

		/****************************************/

		StaticRoutingExtension::RouteChangeTask::RouteChangeTask(CHANGE_TYPE t, StaticRoute *r)
		 : type(t), route(r)
		{

		}

		StaticRoutingExtension::RouteChangeTask::~RouteChangeTask()
		{

		}

		std::string StaticRoutingExtension::RouteChangeTask::toString()
		{
			return "RouteChangeTask: " + (*route).toString();
		}

		/****************************************/

		StaticRoutingExtension::ExpireTask::ExpireTask(size_t t)
		 : timestamp(t)
		{

		}

		StaticRoutingExtension::ExpireTask::~ExpireTask()
		{

		}

		std::string StaticRoutingExtension::ExpireTask::toString()
		{
			std::stringstream ss;
			ss << "ExpireTask: " << timestamp;
			return ss.str();
		}
	}
}