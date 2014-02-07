#define BOOST_LOG_DYN_LINK 1

#ifndef SOURCE_UTIL_LOG_H_
#define SOURCE_UTIL_LOG_H_

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#ifdef test
#undef test
#endif

#include <fstream>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/log/core.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>

#define FDS_LOG(lg) BOOST_LOG_SEV(lg.get_slog(), fds::fds_log::normal)
#define FDS_LOG_SEV(lg, sev) BOOST_LOG_SEV(lg.get_slog(), sev)
#define FDS_PLOG(lg_ptr) BOOST_LOG_SEV(lg_ptr->get_slog(), fds::fds_log::normal)
#define FDS_PLOG_SEV(lg_ptr, sev) BOOST_LOG_SEV(lg_ptr->get_slog(), sev)
#define FDS_PLOG_COMMON(lg_ptr, sev) BOOST_LOG_SEV(lg_ptr->get_slog(), sev) << __FUNCTION__ << " " << __LINE__ << " " << log_string() << " "
#define FDS_PLOG_INFO(lg_ptr) FDS_PLOG_COMMON(lg_ptr, fds::fds_log::normal)
#define FDS_PLOG_WARN(lg_ptr) FDS_PLOG_COMMON(lg_ptr, fds::fds_log::warning)
#define FDS_PLOG_ERR(lg_ptr)  FDS_PLOG_COMMON(lg_ptr, fds::fds_log::error)

//For classes that expose the GetLog() fn .
#define LOGGERPTR  GetLog()
//incase your current logger is different fom GetLog(), 
//redefine macro [LOGGERPTR] at the top of your cpp file
#ifndef DONTLOGLINE
#define _ATLINE_ <<"[" __FILE__ << ":" << __LINE__ << ":" << __FUNCTION__ << "] - "
#else
#define _ATLINE_
#endif

#define LOGTRACE    FDS_PLOG_SEV(LOGGERPTR, fds::fds_log::trace) _ATLINE_
#define LOGDEBUG    FDS_PLOG_SEV(LOGGERPTR, fds::fds_log::debug) _ATLINE_
#define LOGNORMAL   FDS_PLOG_SEV(LOGGERPTR, fds::fds_log::normal) _ATLINE_
#define LOGNOTIFY   FDS_PLOG_SEV(LOGGERPTR, fds::fds_log::notification) _ATLINE_
#define LOGWARN	    FDS_PLOG_SEV(LOGGERPTR, fds::fds_log::warning) _ATLINE_
#define LOGERROR    FDS_PLOG_SEV(LOGGERPTR, fds::fds_log::error) _ATLINE_
#define LOGCRITICAL FDS_PLOG_SEV(LOGGERPTR, fds::fds_log::critical) _ATLINE_

namespace fds {

  class fds_log {
 public:
    enum severity_level {
      trace,
      debug,
      normal,
      notification,
      warning,
      error,
      critical
    };
    
    enum log_options {
      record,
      pid,
      pname,
      tid
    };
    
 private:
    typedef boost::log::sinks::synchronous_sink< boost::log::sinks::text_ostream_backend > text_sink;
    typedef boost::log::sinks::synchronous_sink< boost::log::sinks::text_file_backend > file_sink;
    
    boost::shared_ptr< file_sink > sink;
    boost::log::sources::severity_logger_mt< severity_level > slg;
    
    void init(const std::string& logfile,
              const std::string& logloc,
              bool timestamp,
              bool severity,
              severity_level level,
              bool pname,
              bool pid,
              bool tid,
              bool record);
    
 public:
    /*
     * New log with all defaults.
     */
    fds_log();
    /*
     * Constructs a new log with the file name and
     * all other defaults.
     */
    explicit fds_log(const std::string& logfile);
    /*
     * Constructs new log in specific location.
     */
    fds_log(const std::string& logfile,
            const std::string& logloc);
    /*
     * Constructs new log in specific location.
     */
    fds_log(const std::string& logfile,
                     const std::string& logloc,
                     severity_level level);
    
    ~fds_log();
    
    void setSeverityFilter(const severity_level &level);

    boost::log::sources::severity_logger_mt<severity_level>& get_slog() { return slg; }
  };

  struct HasLogger {
      // get the class logger
      fds_log* GetLog() const;

      // set a new logger & return the old one.
      fds_log* SetLog(fds_log* logptr) const ;
    private:
      mutable fds_log* logptr=NULL;
  };

  typedef boost::shared_ptr<fds_log> fds_logPtr;

}  // namespace fds

#endif  // SOURCE_UTIL_LOG_H_
