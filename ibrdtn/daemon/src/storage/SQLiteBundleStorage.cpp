/*
 * SQLiteBundleStorage.cpp
 *
 *  Created on: 09.01.2010
 *      Author: Myrtus
 */


#include "storage/SQLiteBundleStorage.h"
#include "core/TimeEvent.h"
#include "core/GlobalEvent.h"
#include "core/BundleCore.h"
#include "core/BundleEvent.h"

#include <ibrdtn/data/PayloadBlock.h>
#include <ibrdtn/data/AgeBlock.h>
#include <ibrdtn/data/Serializer.h>
#include <ibrdtn/data/Bundle.h>
#include <ibrdtn/data/BundleID.h>

#include <ibrcommon/thread/MutexLock.h>
#include <ibrcommon/thread/RWLock.h>
#include <ibrcommon/data/BLOB.h>
#include <ibrcommon/Logger.h>
#include <memory>

namespace dtn
{
	namespace storage
	{
		ibrcommon::Mutex SQLiteBundleStorage::TaskIdle::_mutex;
		bool SQLiteBundleStorage::TaskIdle::_idle = false;

		SQLiteBundleStorage::SQLiteBLOB::SQLiteBLOB(const ibrcommon::File &path)
		 : _blobPath(path)
		{
			// generate a new temporary file
			_file = ibrcommon::TemporaryFile(_blobPath, "blob");
		}

		SQLiteBundleStorage::SQLiteBLOB::~SQLiteBLOB()
		{
			// delete the file if the last reference is destroyed
			_file.remove();
		}

		void SQLiteBundleStorage::SQLiteBLOB::clear()
		{
			// close the file
			_filestream.close();

			// remove the old file
			_file.remove();

			// generate a new temporary file
			_file = ibrcommon::TemporaryFile(_blobPath, "blob");

			// open temporary file
			_filestream.open(_file.getPath().c_str(), ios::in | ios::out | ios::trunc | ios::binary );

			if (!_filestream.is_open())
			{
				IBRCOMMON_LOGGER(error) << "can not open temporary file " << _file.getPath() << IBRCOMMON_LOGGER_ENDL;
				throw ibrcommon::CanNotOpenFileException(_file);
			}
		}

		void SQLiteBundleStorage::SQLiteBLOB::open()
		{
			ibrcommon::BLOB::_filelimit.wait();

			// open temporary file
			_filestream.open(_file.getPath().c_str(), ios::in | ios::out | ios::binary );

			if (!_filestream.is_open())
			{
				IBRCOMMON_LOGGER(error) << "can not open temporary file " << _file.getPath() << IBRCOMMON_LOGGER_ENDL;
				throw ibrcommon::CanNotOpenFileException(_file);
			}
		}

		void SQLiteBundleStorage::SQLiteBLOB::close()
		{
			// flush the filestream
			_filestream.flush();

			// close the file
			_filestream.close();

			ibrcommon::BLOB::_filelimit.post();
		}

		size_t SQLiteBundleStorage::SQLiteBLOB::__get_size()
		{
			return _file.size();
		}

		ibrcommon::BLOB::Reference SQLiteBundleStorage::create()
		{
			return ibrcommon::BLOB::Reference(new SQLiteBLOB(_blobPath));
		}

		SQLiteBundleStorage::SQLiteBundleStorage(const ibrcommon::File &path, const size_t &size)
		 : _database(path.get("sqlite.db"), size)
		{
			// set the block path
			_blockPath = path.get("blocks");
			_blobPath = path.get("blob");
		}

		SQLiteBundleStorage::~SQLiteBundleStorage()
		{
			stop();
			join();
		}

		void SQLiteBundleStorage::componentRun()
		{
			// loop until aborted
			try {
				while (true)
				{
					Task *t = _tasks.getnpop(true);

					try {
						BlockingTask &btask = dynamic_cast<BlockingTask&>(*t);
						try {
							btask.run(*this);
						} catch (const std::exception&) {
							btask.abort();
							continue;
						};
						btask.done();
						continue;
					} catch (const std::bad_cast&) { };

					try {
						std::auto_ptr<Task> killer(t);
						t->run(*this);
					} catch (const std::exception&) { };
				}
			} catch (const ibrcommon::QueueUnblockedException &ex) {
				// we are aborted, abort all blocking tasks
			}
		}

		void SQLiteBundleStorage::componentUp()
		{
			//register Events
			bindEvent(dtn::core::TimeEvent::className);
			bindEvent(dtn::core::GlobalEvent::className);

			ibrcommon::RWLock l(_global_lock, ibrcommon::RWMutex::LOCK_READWRITE);

			// delete all old BLOB container
			_blobPath.remove(true);

			// create BLOB folder
			ibrcommon::File::createDirectory( _blobPath );

			// create the bundle folder
			ibrcommon::File::createDirectory( _blockPath );

			// open the database and create all folders and files if needed
			_database.open();
		};

		void SQLiteBundleStorage::componentDown()
		{
			//unregister Events
			unbindEvent(dtn::core::TimeEvent::className);
			unbindEvent(dtn::core::GlobalEvent::className);

			ibrcommon::RWLock l(_global_lock, ibrcommon::RWMutex::LOCK_READWRITE);

			// close the database
			_database.close();
		};

		void SQLiteBundleStorage::__cancellation()
		{
			_tasks.abort();
		}

		const std::set<dtn::data::EID> SQLiteBundleStorage::getDistinctDestinations()
		{
			ibrcommon::RWLock l(_global_lock, ibrcommon::RWMutex::LOCK_READONLY);
			return _database.getDistinctDestinations();
		}

		const std::list<dtn::data::MetaBundle> SQLiteBundleStorage::get(BundleFilterCallback &cb)
		{
			ibrcommon::RWLock l(_global_lock, ibrcommon::RWMutex::LOCK_READONLY);
			return _database.get(cb);
		}

		dtn::data::Bundle SQLiteBundleStorage::get(const dtn::data::BundleID &id)
		{
			ibrcommon::RWLock l(_global_lock, ibrcommon::RWMutex::LOCK_READONLY);

			SQLiteDatabase::blocklist blocks;
			dtn::data::Bundle bundle;

			// query the data base for the bundle
			_database.get(id, bundle, blocks);

			for (SQLiteDatabase::blocklist::const_iterator iter = blocks.begin(); iter != blocks.end(); iter++)
			{
				const SQLiteDatabase::blocklist_entry &entry = (*iter);
				const int blocktyp = entry.first;
				const ibrcommon::File &file = entry.second;

				IBRCOMMON_LOGGER_DEBUG(50) << "add block: " << file.getPath() << IBRCOMMON_LOGGER_ENDL;

				// load block from file
				std::ifstream is(file.getPath().c_str(), std::ios::binary | std::ios::in);

				if ((blocktyp == dtn::data::PayloadBlock::BLOCK_TYPE) &&
						(!bundle.get(dtn::data::PrimaryBlock::APPDATA_IS_ADMRECORD)))
				{
					// create a new BLOB object
					SQLiteBLOB *blob = new SQLiteBLOB(_blobPath);

					// remove the corresponding file
					blob->_file.remove();

					// generate a hardlink, pointing to the BLOB file
					if ( ::link(file.getPath().c_str(), blob->_file.getPath().c_str()) == 0)
					{
						// create a reference of the BLOB
						ibrcommon::BLOB::Reference ref(blob);

						// add payload block to the bundle
						bundle.push_back(ref);
					}
					else
					{
						delete blob;
						IBRCOMMON_LOGGER(error) << "unable to load bundle: failed to create a hard-link" << IBRCOMMON_LOGGER_ENDL;
					}
				}
				else
				{
					// read the block
					dtn::data::Block &block = dtn::data::SeparateDeserializer(is, bundle).readBlock();

					// close the file
					is.close();

					// modify the age block if present
					try {
						dtn::data::AgeBlock &agebl = dynamic_cast<dtn::data::AgeBlock&>(block);

						// modify the AgeBlock with the age of the file
						time_t age = file.lastaccess() - file.lastmodify();

						agebl.addSeconds(age);
					} catch (const std::bad_cast&) { };
				}
			}

			return bundle;
		}

		void SQLiteBundleStorage::store(const dtn::data::Bundle &bundle)
		{
			IBRCOMMON_LOGGER_DEBUG(25) << "store bundle " << bundle.toString() << IBRCOMMON_LOGGER_ENDL;

			ibrcommon::RWLock l(_global_lock, ibrcommon::RWMutex::LOCK_READWRITE);

			// start transaction to store the bundle
			_database.transaction();

			try {
				// store the bundle data in the database
				_database.store(bundle);

				// create a bundle id
				const dtn::data::BundleID id(bundle);

				// get all blocks of the bundle
				const list<const dtn::data::Block*> blocklist = bundle.getBlocks();

				// index number for order of the blocks
				int index = 1;

				// number of bytes stored
				int storedBytes = 0;

				for(std::list<const dtn::data::Block*>::const_iterator it = blocklist.begin() ;it != blocklist.end(); it++)
				{
					const dtn::data::Block &block = (**it);

					// create a temporary file
					ibrcommon::TemporaryFile tmpfile(_blockPath, "block");

					try {
						try {
							const dtn::data::PayloadBlock &payload = dynamic_cast<const dtn::data::PayloadBlock&>(block);
							ibrcommon::BLOB::Reference ref = payload.getBLOB();
							ibrcommon::BLOB::iostream stream = ref.iostream();

							const SQLiteBLOB &blob = dynamic_cast<const SQLiteBLOB&>(*ref);

							// first remove the tmp file
							tmpfile.remove();

							// make a hardlink to the origin blob file
							if ( ::link(blob._file.getPath().c_str(), tmpfile.getPath().c_str()) != 0 )
							{
								tmpfile = ibrcommon::TemporaryFile(_blockPath, "block");
								throw ibrcommon::Exception("hard-link failed");
							}
						} catch (const std::bad_cast&) {
							throw ibrcommon::Exception("not a Payload or SQLiteBLOB");
						}

						storedBytes += _blockPath.size();
					} catch (const ibrcommon::Exception&) {
						std::ofstream filestream(tmpfile.getPath().c_str(), std::ios_base::out | std::ios::binary);
						dtn::data::SeparateSerializer serializer(filestream);
						serializer << block;
						storedBytes += serializer.getLength(block);
						filestream.close();
					}

					_database.store(id, index, block, tmpfile);

					// increment index
					index++;
				}

				_database.commit();

				try {
					// the bundle is stored sucessfully, we could accept custody if it is requested
					const dtn::data::EID custodian = acceptCustody(bundle);

					// update the custody address of this bundle
					_database.update(SQLiteDatabase::UPDATE_CUSTODIAN, bundle, custodian);
				} catch (const ibrcommon::Exception&) {
					// this bundle has no request for custody transfers
				}

				IBRCOMMON_LOGGER_DEBUG(10) << "bundle " << bundle.toString() << " stored" << IBRCOMMON_LOGGER_ENDL;
			} catch (const ibrcommon::Exception&) {
				_database.rollback();
			}
		}

		void SQLiteBundleStorage::remove(const dtn::data::BundleID &id)
		{
			_tasks.push(new TaskRemove(id));
		}

		void SQLiteBundleStorage::TaskRemove::run(SQLiteBundleStorage &storage)
		{
			ibrcommon::RWLock l(storage._global_lock, ibrcommon::RWMutex::LOCK_READWRITE);
			storage._database.remove(_id);
		}

		void SQLiteBundleStorage::clearAll()
		{
			clear();
		}

		void SQLiteBundleStorage::clear()
		{
			ibrcommon::RWLock l(_global_lock, ibrcommon::RWMutex::LOCK_READWRITE);

			_database.clear();

			//Delete Folder SQL_TABLE_BLOCK containing Blocks
			_blockPath.remove(true);
			ibrcommon::File::createDirectory(_blockPath);
		}

		bool SQLiteBundleStorage::empty()
		{
			ibrcommon::RWLock l(_global_lock, ibrcommon::RWMutex::LOCK_READONLY);
			return _database.empty();
		}

		unsigned int SQLiteBundleStorage::count()
		{
			ibrcommon::RWLock l(_global_lock, ibrcommon::RWMutex::LOCK_READONLY);
			return _database.count();
		}

		void SQLiteBundleStorage::raiseEvent(const dtn::core::Event *evt)
		{
			try {
				const dtn::core::TimeEvent &time = dynamic_cast<const dtn::core::TimeEvent&>(*evt);

				if (time.getAction() == dtn::core::TIME_SECOND_TICK)
				{
					_tasks.push(new TaskExpire(time.getTimestamp()));
				}
			} catch (const std::bad_cast&) { }
			
			try {
				const dtn::core::GlobalEvent &global = dynamic_cast<const dtn::core::GlobalEvent&>(*evt);

				if(global.getAction() == dtn::core::GlobalEvent::GLOBAL_IDLE)
				{
					// switch to idle mode
					ibrcommon::MutexLock l(TaskIdle::_mutex);
					TaskIdle::_idle = true;

					// generate an idle task
					_tasks.push(new TaskIdle());
				}
				else if(global.getAction() == dtn::core::GlobalEvent::GLOBAL_BUSY)
				{
					// switch back to non-idle mode
					ibrcommon::MutexLock l(TaskIdle::_mutex);
					TaskIdle::_idle = false;
				}
			} catch (const std::bad_cast&) { }
		}

		void SQLiteBundleStorage::TaskExpire::run(SQLiteBundleStorage &storage)
		{
			ibrcommon::RWLock l(storage._global_lock, ibrcommon::RWMutex::LOCK_READWRITE);
			storage._database.expire(_timestamp);
		}

		void SQLiteBundleStorage::TaskIdle::run(SQLiteBundleStorage &storage)
		{
			// until IDLE is false
			while (true)
			{
				/*
				 * When an object (table, index, trigger, or view) is dropped from the database, it leaves behind empty space.
				 * This empty space will be reused the next time new information is added to the database. But in the meantime,
				 * the database file might be larger than strictly necessary. Also, frequent inserts, updates, and deletes can
				 * cause the information in the database to become fragmented - scrattered out all across the database file rather
				 * than clustered together in one place.
				 * The VACUUM command cleans the main database. This eliminates free pages, aligns table data to be contiguous,
				 * and otherwise cleans up the database file structure.
				 */
				{
					ibrcommon::RWLock l(storage._global_lock, ibrcommon::RWMutex::LOCK_READWRITE);
					storage._database.vacuum();
				}

				// here we can do some IDLE stuff...
				::sleep(1);

				ibrcommon::MutexLock l(TaskIdle::_mutex);
				if (!TaskIdle::_idle) return;
			}
		}

		const std::string SQLiteBundleStorage::getName() const
		{
			return "SQLiteBundleStorage";
		}

		void SQLiteBundleStorage::releaseCustody(const dtn::data::EID &custodian, const dtn::data::BundleID &id)
		{
			ibrcommon::RWLock l(_global_lock, ibrcommon::RWMutex::LOCK_READONLY);

			// custody is successful transferred to another node.
			// it is safe to delete this bundle now. (depending on the routing algorithm.)
			// update the custodian of this bundle with the new one
			_database.update(SQLiteDatabase::UPDATE_CUSTODIAN, id, custodian);
		}
	}
}