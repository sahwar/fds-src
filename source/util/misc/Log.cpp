/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#define BOOST_LOG_DYN_LINK 1

#include <boost/log/expressions.hpp>
#include <boost/log/utility/exception_handler.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/attributes/current_process_name.hpp>
#include <boost/log/attributes/current_process_id.hpp>
#include <boost/log/expressions/formatters/named_scope.hpp>
#include <boost/algorithm/string.hpp>
#include <util/Log.h>
#include <fds_process.h>
#include <dynamic_vers.h>
#include <dynamic_machine_arch.h>

namespace fds {

std::string cleanNameFromPrettyFunc(const std::string& prettyFunction, bool fClassOnly) {
    size_t colons = prettyFunction.find("::");
    if (colons == std::string::npos)
        return "::";
    colons = prettyFunction.find("(");
    if (fClassOnly) {
        // move before the fn.
        colons = prettyFunction.rfind("::", colons);
    }
    size_t begin = prettyFunction.substr(0,colons).rfind(" ") + 1;
    size_t end = colons - begin;

    return prettyFunction.substr(begin,end);
}

__TRACER__::__TRACER__(const std::string& _prettyName, const std::string& _filename, int _lineno)
        : prettyName(cleanNameFromPrettyFunc(_prettyName)),filename(_filename),lineno(_lineno) {
    GLOGDEBUG << "enter : " << prettyName << ":" << filename << ":" << lineno;
}

__TRACER__::~__TRACER__() {
    GLOGDEBUG << "exit  : " << prettyName << ":" << filename << ":" << lineno;
}

/*
 * Rotate log when reachs N bytes
 */
#define ROTATION_SIZE 50 * 1024 * 1024

/*
 * Delete oldest log of a process when directory reaches N Bytes
 * */
#ifdef DEBUG
#define MAX_DIR_SIZE 1024 * 1024 * 1024 * (uint64_t)20
#endif


/*
 * Adds and modifies attributes for the logger stream - helps logger see function, file, and line number as a
 * part of the attributes
 */
std::string set_get_attrib(const char* name, std::string value, const char * function_name) {
    value.append(function_name);
    value.append("] - ");
    auto attr = boost::log::attribute_cast<boost::log::attributes::mutable_constant<std::string>>(boost::log::core::get()->get_thread_attributes()[name]);
    if (attr) {
        attr.set(value);
        return attr.get();
    } else {
        boost::log::core::get()->add_thread_attribute("Location",
                                                      boost::log::attributes::mutable_constant<std::string>(value));
    }
    return value;
}

BOOST_LOG_ATTRIBUTE_KEYWORD(process_name, "ProcessName", std::string)

/*
 * The formatting logic for the severity level
 */
template< typename CharT, typename TraitsT >
inline std::basic_ostream< CharT, TraitsT >& operator<< (std::basic_ostream< CharT, TraitsT >& strm, fds_log::severity_level lvl)
{
    static const char* const str[] =
            {
                "trace",
                "debug",
                "migrate",
                "io",
                "normal",
                "notify",
                "warning",
                "error",
                "CRITICAL"
            };
    if (static_cast< std::size_t >(lvl) < (sizeof(str) / sizeof(*str)))
        strm << str[lvl];
    else
        strm << static_cast< int >(lvl);
    return strm;
}

void writeHeader(boost::log::sinks::text_file_backend::stream_type& file) {
    char buildStr[256];

    if (g_fdsprocess == nullptr) {
        snprintf(buildStr, sizeof(buildStr), buildStrTmpl, "?", versDate, versRev, machineArch);
    } else {
        snprintf(buildStr, sizeof(buildStr), buildStrTmpl,
                 SERVICE_NAME_FROM_ID(), versDate, versRev, machineArch);
    }

    file << buildStr << std::endl;

}

fds_log::severity_level fds_log::getLevelFromName(std::string level) {
    boost::to_upper(level);
    boost::trim(level);
    if (0 == level.compare(0, 5, "TRACE")) return trace;
    if (0 == level.compare(0, 5, "DEBUG")) return debug;
    if (0 == level.compare(0, 7, "MIGRATE")) return migrate;
    if (0 == level.compare(0, 2, "IO")) return io;
    if (0 == level.compare(0, 6, "NORMAL")) return normal;
    if (0 == level.compare(0, 4, "INFO")) return normal;
    if (0 == level.compare(0, 5, "NOTIF")) return notification;
    if (0 == level.compare(0, 4, "WARN")) return warning;
    if (0 == level.compare(0, 5, "ERROR")) return error;
    if (0 == level.compare(0, 4, "CRIT")) return critical;

    // to support numbers
    if (level.size() == 1) {
        int value = level[0] - '0';
        if (value >= trace  && value <= critical) return (fds_log::severity_level)value;
    }

    // default
    return normal;
}

void fds_log::init(const std::string& logfile,
                   const std::string& logloc,
                   bool timestamp,
                   bool severity,
                   severity_level level,
                   bool Pname,
                   bool Pid,
                   bool Tid,
                   bool Record) {
    /*
     * Create the with file name and rotation.
     */
    sink = boost::make_shared< file_sink >(boost::log::keywords::file_name = logfile + "_%N.log",
                                           boost::log::keywords::rotation_size = ROTATION_SIZE);

    /*
     * Setup log sub-directory location
     */
#ifdef DEBUG
    /*
     * NOTE: From local testing MAX_DIR_SIZE and the collector will ONLY affect the matching fdslog files
     * i.e. SM collector will only affect SM logs and DM collector will only affect DM logs. I am enclosing this
     * in DEBUG for the time being to make sure that this does not
     */
    if (logloc.empty()) {
        sink->locked_backend()->set_file_collector(boost::log::sinks::file::make_collector(
            boost::log::keywords::target = ".", boost::log::keywords::max_size = MAX_DIR_SIZE));
    } else {
        sink->locked_backend()->set_file_collector(boost::log::sinks::file::make_collector(
            boost::log::keywords::target = logloc, boost::log::keywords::max_size = MAX_DIR_SIZE));
    }
#else
    if (logloc.empty()) {
        sink->locked_backend()->set_file_collector(boost::log::sinks::file::make_collector(
            boost::log::keywords::target = "."));
    } else {
        sink->locked_backend()->set_file_collector(boost::log::sinks::file::make_collector(
            boost::log::keywords::target = logloc));
    }
#endif

    /*
     * Set to defaulty scan for existing log files and
     * pickup where it left previously off.
     */
    sink->locked_backend()->scan_for_files();

#ifdef DEBUG
    sink->locked_backend()->auto_flush(true);
#endif

    /*
     * Set the filter to not print messages below
     * a certain level.
     */
    sink->set_filter(
        boost::log::expressions::attr<severity_level>("Severity").or_default(normal) >= level);
    severityLevel = level;

    /*
     * Setup the attributes - Location will only be set to non empty values if the build is debug
     */
    boost::log::attributes::counter< unsigned int > RecordID(1);
    boost::log::core::get()->add_global_attribute("RecordID", RecordID);
    boost::log::attributes::local_clock TimeStamp;
    boost::log::core::get()->add_global_attribute("TimeStamp", TimeStamp);
    boost::log::core::get()->add_global_attribute("ProcessName",
                                                  boost::log::attributes::current_process_name());
    boost::log::core::get()->add_global_attribute("ProcessID",
                                                  boost::log::attributes::current_process_id());
    boost::log::core::get()->add_global_attribute("ThreadID",
                                                  boost::log::attributes::current_thread_id());
    boost::log::core::get()->add_global_attribute("Context",
                                                  boost::log::attributes::named_scope());

    /*
     * Set the format
     */
    sink->set_formatter(boost::log::expressions::stream
                        << "["
                        << boost::log::expressions::format_date_time< boost::posix_time::ptime >("TimeStamp", "%d.%m.%Y %H:%M:%S.%f")
                        << "] [" << boost::log::expressions::attr< severity_level >("Severity")
                        << "] ["
                        << boost::log::expressions::attr< boost::log::attributes::current_thread_id::value_type >("ThreadID")
                        << "]"
                        << boost::log::expressions::attr< std::string >("Location")
                        << boost::log::expressions::format_named_scope("Context",
                                                                boost::log::keywords::format = "%n",
                                                                boost::log::keywords::delimiter = " ",
                                                                boost::log::keywords::iteration = boost::log::expressions::reverse)
                        << " "
                        << boost::log::expressions::smessage);

    /*
     * Set the log header.
     */
    sink->locked_backend()->set_open_handler(&writeHeader);

    /*
     * If we are a DEBUG build we want a core dump if we get an exception,
     * otherwise let's just keep going without logging...?
     */
#ifdef DEBUG
    boost::log::core::get()->set_exception_handler([] { abort(); });
#else
    boost::log::core::get()->set_exception_handler(boost::log::make_exception_suppressor());
#endif

    /*
     * Add the sink to the core.
     */
    boost::log::core::get()->add_sink(sink);
}

fds_log::fds_log(const std::string& logfile,
                 const std::string& logloc,
                 severity_level level) {
    init(logfile,
         logloc,
         true,   // timestamp
         true,   // severity
         level, // minimum sev level
         false,  // process name
         false,  // process id
         true,   // thread id
         false); // record id
}

fds_log::~fds_log() {
}

void fds_log::setSeverityFilter(const severity_level &level) {
    sink->reset_filter();
    sink->set_filter(
        boost::log::expressions::attr<severity_level>("Severity").or_default(normal) >= level);
    severityLevel= level;
}

void fds_log::rotate() {
    sink->locked_backend()->rotate_file();
}

}  // namespace fds
