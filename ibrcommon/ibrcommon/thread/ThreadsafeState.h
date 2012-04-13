/*
 * ThreadsafeState.h
 *
 *  Created on: 13.05.2011
 *      Author: morgenro
 */

#ifndef THREADSAFESTATE_H_
#define THREADSAFESTATE_H_

#include <ibrcommon/thread/MutexLock.h>
#include <ibrcommon/thread/Conditional.h>

namespace ibrcommon
{
	template <class T>
	class ThreadsafeState : protected ibrcommon::Conditional
	{
	protected:
		T _state;
		T _final_state;

	public:
		ThreadsafeState(T init, T final)
		 : _state(init), _final_state(final)
		{
		};

		virtual ~ThreadsafeState()
		{
			ibrcommon::MutexLock l(*this);
			_state = _final_state;
			this->signal(true);
		};

		T get() const
		{
			return _state;
		}

		void wait(T st)
		{
			ibrcommon::MutexLock l(*this);
			while (_state != st)
			{
				if (_state == _final_state) return;
				this->wait();
			}
		}

		T operator=(T st)
		{
			// return, if the final state is reached
			if (_state == _final_state) return _state;

			ibrcommon::MutexLock l(*this);
			_state = st;
			this->signal(true);
			return _state;
		}

		bool operator==(T st) const
		{
			return (st == _state);
		}

		bool operator!=(T st) const
		{
			return (st != _state);
		}

		class Locked
		{
		private:
			ThreadsafeState<T> &_tss;
			MutexLock _lock;

		public:
			Locked(ThreadsafeState<T> &tss)
			 : _tss(tss), _lock(tss)
			{
			};

			virtual ~Locked()
			{
				_tss.signal(true);
			};

			T get() const
			{
				return _tss._state;
			}

			T operator=(T st)
			{
				// return, if the final state is reached
				if (_tss._state == _tss._final_state) return _tss._state;

				_tss._state = st;
				return _tss._state;
			}

			bool operator==(T st)
			{
				return (_tss._state == st);
			}

			bool operator!=(T st)
			{
				return (_tss._state != st);
			}

			void wait(size_t st)
			{
				while (!(_tss._state & st))
				{
					if (_tss._state == _tss._final_state) return;
					((Conditional&)_tss).wait();
				}
			}

			void wait(T st)
			{
				while (_tss._state != st)
				{
					if (_tss._state == _tss._final_state) return;
					((Conditional&)_tss).wait();
				}
			}
		};

		Locked lock()
		{
			return *this;
		}
	};
}

#endif /* THREADSAFESTATE_H_ */