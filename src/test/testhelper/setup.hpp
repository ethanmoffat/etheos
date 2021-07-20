#pragma once

#include "config.hpp"
#include "eoserv_config.hpp"

static void CreateConfigWithTestDefaults(Config& config, Config& admin_config)
{
    eoserv_config_validate_config(config);
    eoserv_config_validate_admin(admin_config);

    // these are needed for the test to run
    // test assumes relative to directory {install_dir}/test
    config["ServerLanguage"] = "../lang/en.ini";
    config["EIF"] = "../data/pub/empty.eif";
    config["ENF"] = "../data/pub/empty.enf";
    config["ESF"] = "../data/pub/empty.esf";
    config["ECF"] = "../data/pub/empty.ecf";

    //turn off SLN
    config["SLN"] = "false";
    // turn off timed save
    config["TimedSave"] = "false";
}