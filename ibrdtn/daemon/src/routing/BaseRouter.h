/*
 * BaseRouter.h
 *
 * Copyright (C) 2011 IBR, TU Braunschweig
 *
 * Written-by: Johannes Morgenroth <morgenroth@ibr.cs.tu-bs.de>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef BASEROUTER_H_
#define BASEROUTER_H_

#include "Component.h"
#include "routing/NeighborDatabase.h"
#include "routing/NodeHandshake.h"
#include "core/EventReceiver.h"
#include "storage/BundleStorage.h"

#include <ibrdtn/data/BundleSet.h>
#include <ibrdtn/data/DTNTime.h>
#include <ibrdtn/data/BundleID.h>
#include <ibrdtn/data/MetaBundle.h>

#include <ibrcommon/thread/Thread.h>
#include <ibrcommon/thread/Conditional.h>


namespace dtn
{
	namespace routing
	{
		class NodeHandshakeExtension;

		class BaseRouter : public dtn::core::EventReceiver, public dtn::daemon::IntegratedComponent
		{
		public:
			class RoutingException : public ibrcommon::Exception
			{
				public:
					RoutingException(string what = "An error occured during routing.") throw() : Exception(what)
					{
					};
			};

			/**
			 * If no neighbour was found, this exception is thrown.
			 */
			class NoNeighbourFoundException : public RoutingException
			{
				public:
					NoNeighbourFoundException(string what = "No neighbour was found.") throw() : RoutingException(what)
					{
					};
			};

			/**
			 * If no route can be found, this exception is thrown.
			 */
			class NoRouteFoundException : public RoutingException
			{
				public:
					NoRouteFoundException(string what = "No route found.") throw() : RoutingException(what)
					{
					};
			};

			class Extension
			{
			public:
				Extension();
				virtual ~Extension() = 0;

				virtual void notify(const dtn::core::Event *evt) = 0;

				enum CALLBACK_ACTION
				{
					ROUTE_CALLBACK_FORWARDED = 0,
					ROUTE_CALLBACK_ABORTED = 1,
					ROUTE_CALLBACK_REJECTED = 2,
					ROUTE_CALLBACK_DELETED = 3
				};

				/**
				 * If some data of another node is required. These method is called to collect all
				 * necessary identifier of data items.
				 * @param The EID of the other node.
				 * @param The object to put the requested identifier into.
				 */
				virtual void requestHandshake(const dtn::data::EID&, NodeHandshake&) const { };

				/**
				 * If a handshake message is received, this method is called to collect the different
				 * data items generated by the router extensions.
				 * @param The EID of the other node.
				 * @param The received handshake object.
				 * @param The response for the received handshake.
				 */
				virtual void responseHandshake(const dtn::data::EID&, const NodeHandshake&, NodeHandshake&) { };

				/**
				 * After a handshake has been completed every module can process the handshake response.
				 * @param The EID of the other node.
				 * @param The received handshake object.
				 */
				virtual void processHandshake(const dtn::data::EID&, NodeHandshake&) { };

				/**
				 * Transfer one bundle to another node.
				 * @throw BundleNotFoundException if the bundle do not exist.
				 * @param destination The EID of the other node.
				 * @param id The ID of the bundle to transfer. This bundle must be stored in the storage.
				 */
				void transferTo(const dtn::data::EID &destination, const dtn::data::BundleID &id);
				void transferTo(NeighborDatabase::NeighborEntry &entry, const dtn::data::BundleID &id);

				/**
				 * Returns true, if the bundle contains routing data.
				 * @param meta
				 * @return
				 */
				static bool isRouting(const dtn::data::EID &eid);

			protected:
				BaseRouter& operator*();

			private:
				friend class BaseRouter;
				static BaseRouter *_router;
			};

			class ThreadedExtension : public Extension, public ibrcommon::JoinableThread
			{
			public:
				ThreadedExtension();
				virtual ~ThreadedExtension() = 0;

				virtual void notify(const dtn::core::Event *evt) = 0;
			};

			class Endpoint
			{
			public:
				Endpoint();
				virtual ~Endpoint() = 0;
			};

		private:
			class VirtualEndpoint
			{
			public:
				VirtualEndpoint(dtn::data::EID name);
				virtual ~VirtualEndpoint();

				ibrcommon::Mutex _clientlock;
				Endpoint *_client;

				dtn::data::EID _name;
				dtn::data::DTNTime _lastseen;
				dtn::data::DTNTime _expire;
			};

		public:
			BaseRouter(dtn::storage::BundleStorage &storage);
			~BaseRouter();

			/**
			 * Add a routing extension to the routing core.
			 * @param extension
			 */
			void addExtension(BaseRouter::Extension *extension);

			/**
			 * Returns a reference to all extensions.
			 * @return
			 */
			const std::list<BaseRouter::Extension*>& getExtensions() const;

			/**
			 * method to receive new events from the EventSwitch
			 */
			void raiseEvent(const dtn::core::Event *evt) throw ();

			/**
			 * Get a bundle out of the storage.
			 * @param id The ID of the bundle.
			 * @return The requested bundle.
			 */
			dtn::data::Bundle getBundle(const dtn::data::BundleID &id);

			dtn::storage::BundleStorage &getStorage();

			/**
			 * Request a neighbor handshake
			 * @param eid
			 */
			void doHandshake(const dtn::data::EID &eid);

			/**
			 * This method returns true, if the given BundleID is known.
			 * @param id
			 * @return
			 */
			bool isKnown(const dtn::data::BundleID &id);

			/**
			 * check if a bundle is known
			 * if the bundle is unkown add it to the known list and return false
			 * @param id
			 * @return
			 */
			bool filterKnown(const dtn::data::MetaBundle &meta);

			/**
			 * This method add a BundleID to the set of known bundles
			 * @param id
			 */
			void setKnown(const dtn::data::MetaBundle &meta);

			/**
			 * Get a vector (bloomfilter) of all known bundles.
			 * @return
			 */
			const dtn::data::BundleSet getKnownBundles();

			/**
			 * Get a vector (bloomfilter) of all purged bundles.
			 * @return
			 */
			const dtn::data::BundleSet getPurgedBundles();

			/**
			 * Add a bundle to the purge vector of this daemon.
			 * @param meta The bundle to purge.
			 */
			void addPurgedBundle(const dtn::data::MetaBundle &meta);

			/**
			 * @see Component::getName()
			 */
			virtual const std::string getName() const;

			/**
			 * Access to the neighbor database. Where several data about the neighbors is stored.
			 * @return
			 */
			NeighborDatabase& getNeighborDB();

		protected:
			virtual void componentUp() throw ();
			virtual void componentDown() throw ();

		private:
			void __forward_event(const dtn::core::Event *evt) const throw ();

			ibrcommon::Mutex _known_bundles_lock;
			dtn::data::BundleSet _known_bundles;

			ibrcommon::Mutex _purged_bundles_lock;
			dtn::data::BundleSet _purged_bundles;

			dtn::storage::BundleStorage &_storage;
			std::list<BaseRouter::Extension*> _extensions;

			NeighborDatabase _neighbor_database;
			NodeHandshakeExtension *_nh_extension;
		};
	}
}


#endif /* BASEROUTER_H_ */
