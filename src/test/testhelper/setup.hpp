#pragma once

#include "config.hpp"
#include "eoserv_config.hpp"

static void CreateConfigWithTestDefaults(Config& config, Config& admin_config)
{
    eoserv_config_validate_config(config);

    // these are needed for the test to run
    // test assumes relative to directory {install_dir}/test
    config["ServerLanguage"] = "../lang/en.ini";
    config["EIF"] = "../data/pub/dat001.eif";
    config["ENF"] = "../data/pub/dtn001.enf";
    config["ESF"] = "../data/pub/dsl001.esf";
    config["ECF"] = "../data/pub/dat001.ecf";

    //turn off SLN
    config["SLN"] = "false";
    // turn off timed save
    config["TimedSave"] = "false";
}