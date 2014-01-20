/* Copyright 2013 Formation Data Systems, Inc.
 */
#ifndef _FDS_COUNTERS_H
#define _FDS_COUNTERS_H

#include <iostream>
#include <atomic>
#include <vector>
#include <boost/noncopyable.hpp>
#include <fds_assert.h>
#include <concurrency/Mutex.h>

namespace fds {
/* Forward declarations */
class FdsCounters;
class FdsBaseCounter;
class FdsCountersMgr;

/**
 * @brief Counter manager.  Mananges the job of exporting registered
 * FdsCounters class objects in various different formats.
 * Supported format are:
 * -Graphite
 */
class FdsCountersMgr : public boost::noncopyable {
 public:
  FdsCountersMgr();
  void add_for_export(FdsCounters *counters);
  void remove_from_export(FdsCounters *counters);

  std::string export_as_graphite();

 protected:
  /* Counter objects that are exported out */
  std::vector<FdsCounters*> exp_counters_;
  /* Lock for this object */
  fds_mutex lock_;
};

/**
 * @brief Base counters class.  Any module that has a set of counters
 * should derive from this class
 */
class FdsCounters : public boost::noncopyable { 
 public:
  FdsCounters(const std::string &id, FdsCountersMgr *mgr)
      : id_(id)
  {
      if (mgr) {
          mgr->add_for_export(this);
      }
  }
  
  std::string id() const
  {
      return id_;
  }

 protected:
  /**
   * @brief Marks the counter for export.  Only export counters
   * that are members of the derived class.  This method is invoked
   * by FdsBaseCounter constructor to mark the counter as exported.
   *
   * @param cp Counter to export
   */
  void add_for_export(FdsBaseCounter* cp)
  {
      fds_assert(cp);
      exp_counters_.push_back(cp);
  }

  void remove_from_export(FdsBaseCounter* cp)
  {
      fds_verify(!"Not implemented yet");
  }

 protected:
  /* Exported counters */
  std::vector<FdsBaseCounter*> exp_counters_;
  /* Id of this counter set */
  std::string id_;
  
  friend class FdsBaseCounter;
  friend class FdsCountersMgr;
};

/**
 * @brief Base class for counters that are to be exported.
 */
class FdsBaseCounter : public boost::noncopyable {
 public:
  /**
   * @brief  Base counter constructor.  Enables a counter to
   * be exported with an identifier.  If export_parent is NULL
   * counter will not be exported.
   *
   * @param id - id to use when exporting the counter
   * @param export_parent - Pointer to the parent.  If null counter
   * is not exported.
   */
  FdsBaseCounter(const std::string &id, FdsCounters *export_parent)
      : id_(id)
  {
      if (export_parent) {
          export_parent->add_for_export(this);
      }
  }

  virtual uint64_t value() const = 0;
  virtual std::string id() const
  {
      return id_;
  }

 private:
  std::string id_;
};


/**
 * @brief Numeric counter
 */
class NumericCounter : public FdsBaseCounter
{
 public:
  NumericCounter(const std::string &id, FdsCounters *export_parent)
      : FdsBaseCounter(id, export_parent)
  {
  }

  virtual uint64_t value() const override
  {
      return val_.load();
  }

  inline void incr() {
      val_++;
  } 

 private:
  std::atomic<uint64_t> val_;
};
}  // namespace fds

#endif
