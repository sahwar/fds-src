/*
 * Copyright 2016 Formation Data Systems, Inc.
 */

#include <platform/environment.h>
#include <boost/tokenizer.hpp>

// this is just to get the Enum for Services
#include <fdsp/health_monitoring_api_types.h>
#include <platform/platform_manager.h>

namespace fds {


Environment* Environment::e = 0;

void Environment::initialize() {
    GLOGDEBUG << "Initializeing environment variable configurations";

    auto config(MODULEPROVIDER()->get_conf_helper());
    config.set_base_path("fds.pm.environment");

    std::string temp;
    std::string dummy("");

    temp = config.get<std::string>("am", dummy);
    if (temp.size() != 0) {
        ingest(pm::BARE_AM, temp);
    }

    temp = config.get<std::string>("dm", dummy);
    if (temp.size() != 0) {
        ingest(pm::DATA_MANAGER, temp);
    }

    temp = config.get<std::string>("sm", dummy);
    if (temp.size() != 0) {
        ingest(pm::STORAGE_MANAGER, temp);
    }

    temp = config.get<std::string>("xdi", dummy);
    if (temp.size() != 0) {
        ingest(pm::JAVA_AM, temp);
    }

}

/* takes a sting of the form "key1=val1,key2=val2,..." and produces a map with pairs (key1, val1) (key2, val2) ...*/
void Environment::ingest(int idx, std::string str){
    EnvironmentMap& env = getEnvironmentForService(idx);

    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
    boost::char_separator<char> sep(",=");

    tokenizer tokens(str, sep);

    for (tokenizer::iterator tok_iter = tokens.begin(); tok_iter != tokens.end(); ++tok_iter) {
        // NOTE: this is pretty disgusting. sorry :'(
        env[*tok_iter] = (++tok_iter != tokens.end()) ? *tok_iter : "";
    }
}

EnvironmentMap& Environment::getEnvironmentForService(int service) {
    return getEnv()->envs[service];
}

void Environment::setEnvironment(EnvironmentMap& env) {
    if (env.size() != 0) {
        for(auto i : env) {
            GLOGDEBUG << "Launching with env var " << i.first << "=" << i.second;
            setenv(i.first.c_str(), i.second.c_str(), 1);
        }
    }
}

void Environment::setEnvironment(int service) {
    GLOGDEBUG << "Called for service index: " << service;
    setEnvironment(getEnvironmentForService(service));
}

}  // namespace fds
