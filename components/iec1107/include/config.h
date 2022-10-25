#pragma once

/*
 * @brief You can add your obis codes that you want to export.
 * If the obis code you added is not taken from the electric
 * meter, its value will not be parsed.
 *
 */
const char* export_obis_code[] =
{
    "32.7.0",
    "1.8.0",
    "34.7.0",
    "96.77.2*1",
};

// Don't change anything about below line!.
const int export_params_size = sizeof(export_obis_code) / sizeof(export_obis_code[0]);
