#pragma once

#include "SingleLock.h"
#include "Helpers.h"
#include "SystemClock.h"

//#include <windows.h>

namespace XR
{
  /**
   * This is condition variable implementation that uses the underlying
   *  Windows mechanisms and assumes Vista (or later)
   */
  class ConditionVariable : public NonCopyable
  {
    CONDITION_VARIABLE cond;

    // SleepConditionVarialbeCS requires the condition variable be entered
    //  only once.
    struct AlmostExit 
    {
      unsigned int count;
      CCriticalSection& cc;
      inline AlmostExit(CCriticalSection& pcc) : count(pcc.exit(1)), cc(pcc) { cc.count = 0; }
      inline ~AlmostExit() { cc.count = 1; cc.restore(count); }
    };

  public:
    inline ConditionVariable() { InitializeConditionVariable(&cond); }

    // apparently, windows condition variables do not need to be deleted
    inline ~ConditionVariable() { }

    inline void wait(CCriticalSection& lock) 
    { 
      AlmostExit ae(lock);
      // even the windows implementation is capable of spontaneous wakes
      SleepConditionVariableCS(&cond,&lock.get_underlying().mutex,INFINITE);
    }

    inline bool wait(CCriticalSection& lock, unsigned long milliseconds) 
    { 
      AlmostExit ae(lock);
      return SleepConditionVariableCS(&cond,&lock.get_underlying().mutex,milliseconds) ? true : false;
    }

    inline void wait(XR::CSingleLock& lock) { wait(lock.get_underlying()); }
    inline bool wait(XR::CSingleLock& lock, unsigned long milliseconds) { return wait(lock.get_underlying(), milliseconds); }
    inline void notifyAll() { WakeAllConditionVariable(&cond); }
    inline void notify() { WakeConditionVariable(&cond); }
  };



    /**
   * This is a condition variable along with its predicate. This allows the use of a 
   *  condition variable without the spurious returns since the state being monitored
   *  is also part of the condition.
   *
   * L should implement the Lockable concept
   *
   * The requirements on P are that it can act as a predicate (that is, I can use
   *  it in an 'while(!predicate){...}' where 'predicate' is of type 'P').
   */
  template <typename P> class TightConditionVariable
  {
    ConditionVariable& cond;
    P predicate;

  public:
    inline TightConditionVariable(ConditionVariable& cv, P predicate_) : cond(cv), predicate(predicate_) {}

    template <typename L> inline void wait(L& lock) { while(!predicate) cond.wait(lock); }
    template <typename L> inline bool wait(L& lock, unsigned long milliseconds)
    {
      bool ret = true;
      if (!predicate)
      {
        if (!milliseconds)
        {
          cond.wait(lock,milliseconds /* zero */);
          return !(!predicate); // eh? I only require the ! operation on P
        }
        else
        {
          EndTime endTime((unsigned int)milliseconds);
          for (bool notdone = true; notdone && ret == true;
               ret = (notdone = (!predicate)) ? ((milliseconds = endTime.MillisLeft()) != 0) : true)
            cond.wait(lock,milliseconds);
        }
      }
      return ret;
    }

    inline void notifyAll() { cond.notifyAll(); }
    inline void notify() { cond.notify(); }
  };
}
