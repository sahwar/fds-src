/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#ifndef SOURCE_UTIL_CONCURRENCY_MUTEX_H_
#define SOURCE_UTIL_CONCURRENCY_MUTEX_H_

#include <boost/noncopyable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>

#include <string>

namespace fds {
  /*
   * Basic mutex class. It is based on the basic
   * boost mutex structure. It is the most basic
   * locking mechanism available.
   */
  class fds_mutex : boost::noncopyable {
 private:
    boost::mutex *_m;
    std::string _name;

    /*
     * Returns a boost reference to the underlying mutex.
     */
    boost::mutex &boost() { return *_m; }

 public:
    explicit fds_mutex(const std::string& name);
    explicit fds_mutex(const char *name);
    ~fds_mutex();

    std::string get_name() const;

    void lock();
    void unlock();
    bool try_lock();

    /*
     * A scoped lock provides RAII-style locking
     * in that locks are acquired on resource
     * allocation and always released upon resource
     * destruction.
     */
    class scoped_lock : boost::noncopyable {
   private:
      boost::mutex::scoped_lock _l;

   public:
      fds_mutex * const _mut;

      /*
       * The constructor creates a boost scoped lock
       * using the mutex provided.
       */
      explicit scoped_lock(fds_mutex &m)  // NOLINT(*)
          : _l(m.boost()),
          _mut(&m) {
      }

      ~scoped_lock() {
      }

      /*
       * Returns a boost reference to the underlying lock;
       */
      boost::mutex::scoped_lock &boost() { return _l; }
    };
  };

  /*
   * Ensure the scoped_lock falls back to our
   * basic scoped_lock.
   */
  typedef fds_mutex::scoped_lock fds_scoped_lock;
}  // namespace fds

#endif  // SOURCE_UTIL_CONCURRENCY_MUTEX_H_
