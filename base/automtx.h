#pragma once
#ifndef _AUTOMTX_H
#define _AUTOMTX_H __FILE__
namespace base
{
    const static int UNLOCKED = 1;
    const static int LOCKED = 0;
    template<class T>
    class AutoMutex
    {
    private:
        T* _mtx;
        bool _isLocked;

    public:
        void lock()
        {
            if(!_isLocked)
                _mtx->lock();
            _isLocked = true;
        }

        void unlock()
        {
            if(_isLocked)
                _mtx->unlock();
            _isLocked = false;
        }

        AutoMutex(T* mtx, int locked) :
            _isLocked(false),
            _mtx(mtx)
        {
            if(locked) lock();
        }

        AutoMutex(T& mtx, int locked) :
            _isLocked(false),
            _mtx(&mtx)
        {
            if(locked) lock();
        }

        AutoMutex(T* mtx) :
            _isLocked(false),
            _mtx(mtx)
        {
            lock();
        }

        AutoMutex(T& mtx) :
            _isLocked(false),
            _mtx(&mtx)
        {
            lock();
        }

        AutoMutex() :
            _mtx(NULL),
            _isLocked(false)
        {
            _mtx = NULL;
        }

        ~AutoMutex()
        {
            unlock();
        }
    };
}
#endif