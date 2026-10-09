/*
 * NOT_FROM_ORIGINAL_SOURCE: coduox: self getGeoLocation(type), the
 * CoD4x-compatible player method that reports where a player's IPv4 address is
 * registered.
 *
 *   0  two-letter country code       "DE"
 *   1  three-letter country code     "DEU"
 *   2  country name                  "Germany"
 *   3  two-letter continent code     "EU"
 *   any other value: the country index as an integer
 *
 * The engine resolves the address in the GeoIP.dat legacy country database
 * and returns an index into the table below, whose order is fixed by that
 * file format. Index 0 means unknown: no database, a bot, or an address the
 * database does not list. It reports "--", "--", "N/A", and "--".
 */

#include "coduomp_player_geolocation.h"

#include "recovered_game.h"
#include "game_functions.h"
#include "scr_vm.h"
#include "g_syscalls.h"

typedef enum coduomp_geolocation_type_e {
    CODUOMP_GEOLOCATION_CODE = 0,
    CODUOMP_GEOLOCATION_CODE3 = 1,
    CODUOMP_GEOLOCATION_NAME = 2,
    CODUOMP_GEOLOCATION_CONTINENT = 3
} coduomp_geolocation_type_t;

typedef struct coduomp_geolocation_country_s {
    const char *code;
    const char *code3;
    const char *name;
    const char *continent;
} coduomp_geolocation_country_t;

enum { CODUOMP_GEOLOCATION_COUNTRY_COUNT = 256 };

static const coduomp_geolocation_country_t
    coduomp_geolocation_countries[CODUOMP_GEOLOCATION_COUNTRY_COUNT] = {
    { "--", "--", "N/A", "--" },  /* 0 */
    { "AP", "AP", "Asia/Pacific Region", "AS" },
    { "EU", "EU", "Europe", "EU" },
    { "AD", "AND", "Andorra", "EU" },
    { "AE", "ARE", "United Arab Emirates", "AS" },
    { "AF", "AFG", "Afghanistan", "AS" },
    { "AG", "ATG", "Antigua and Barbuda", "NA" },
    { "AI", "AIA", "Anguilla", "NA" },
    { "AL", "ALB", "Albania", "EU" },
    { "AM", "ARM", "Armenia", "AS" },
    { "CW", "CUW", "Curacao", "NA" },  /* 10 */
    { "AO", "AGO", "Angola", "AF" },
    { "AQ", "ATA", "Antarctica", "AN" },
    { "AR", "ARG", "Argentina", "SA" },
    { "AS", "ASM", "American Samoa", "OC" },
    { "AT", "AUT", "Austria", "EU" },
    { "AU", "AUS", "Australia", "OC" },
    { "AW", "ABW", "Aruba", "NA" },
    { "AZ", "AZE", "Azerbaijan", "AS" },
    { "BA", "BIH", "Bosnia and Herzegovina", "EU" },
    { "BB", "BRB", "Barbados", "NA" },  /* 20 */
    { "BD", "BGD", "Bangladesh", "AS" },
    { "BE", "BEL", "Belgium", "EU" },
    { "BF", "BFA", "Burkina Faso", "AF" },
    { "BG", "BGR", "Bulgaria", "EU" },
    { "BH", "BHR", "Bahrain", "AS" },
    { "BI", "BDI", "Burundi", "AF" },
    { "BJ", "BEN", "Benin", "AF" },
    { "BM", "BMU", "Bermuda", "NA" },
    { "BN", "BRN", "Brunei", "AS" },
    { "BO", "BOL", "Bolivia", "SA" },  /* 30 */
    { "BR", "BRA", "Brazil", "SA" },
    { "BS", "BHS", "Bahamas", "NA" },
    { "BT", "BTN", "Bhutan", "AS" },
    { "BV", "BVT", "Bouvet Island", "AN" },
    { "BW", "BWA", "Botswana", "AF" },
    { "BY", "BLR", "Belarus", "EU" },
    { "BZ", "BLZ", "Belize", "NA" },
    { "CA", "CAN", "Canada", "NA" },
    { "CC", "CCK", "Cocos (Keeling) Islands", "AS" },
    { "CD", "COD", "DR Congo", "AF" },  /* 40 */
    { "CF", "CAF", "Central African Republic", "AF" },
    { "CG", "COG", "Congo", "AF" },
    { "CH", "CHE", "Switzerland", "EU" },
    { "CI", "CIV", "Ivory Coast", "AF" },
    { "CK", "COK", "Cook Islands", "OC" },
    { "CL", "CHL", "Chile", "SA" },
    { "CM", "CMR", "Cameroon", "AF" },
    { "CN", "CHN", "China", "AS" },
    { "CO", "COL", "Colombia", "SA" },
    { "CR", "CRI", "Costa Rica", "NA" },  /* 50 */
    { "CU", "CUB", "Cuba", "NA" },
    { "CV", "CPV", "Cape Verde", "AF" },
    { "CX", "CXR", "Christmas Island", "AS" },
    { "CY", "CYP", "Cyprus", "AS" },
    { "CZ", "CZE", "Czech Republic", "EU" },
    { "DE", "DEU", "Germany", "EU" },
    { "DJ", "DJI", "Djibouti", "AF" },
    { "DK", "DNK", "Denmark", "EU" },
    { "DM", "DMA", "Dominica", "NA" },
    { "DO", "DOM", "Dominican Republic", "NA" },  /* 60 */
    { "DZ", "DZA", "Algeria", "AF" },
    { "EC", "ECU", "Ecuador", "SA" },
    { "EE", "EST", "Estonia", "EU" },
    { "EG", "EGY", "Egypt", "AF" },
    { "EH", "ESH", "Western Sahara", "AF" },
    { "ER", "ERI", "Eritrea", "AF" },
    { "ES", "ESP", "Spain", "EU" },
    { "ET", "ETH", "Ethiopia", "AF" },
    { "FI", "FIN", "Finland", "EU" },
    { "FJ", "FJI", "Fiji", "OC" },  /* 70 */
    { "FK", "FLK", "Falkland Islands", "SA" },
    { "FM", "FSM", "Micronesia", "OC" },
    { "FO", "FRO", "Faroe Islands", "EU" },
    { "FR", "FRA", "France", "EU" },
    { "SX", "SXM", "Sint Maarten", "NA" },
    { "GA", "GAB", "Gabon", "AF" },
    { "GB", "GBR", "United Kingdom", "EU" },
    { "GD", "GRD", "Grenada", "NA" },
    { "GE", "GEO", "Georgia", "AS" },
    { "GF", "GUF", "French Guiana", "SA" },  /* 80 */
    { "GH", "GHA", "Ghana", "AF" },
    { "GI", "GIB", "Gibraltar", "EU" },
    { "GL", "GRL", "Greenland", "NA" },
    { "GM", "GMB", "Gambia", "AF" },
    { "GN", "GIN", "Guinea", "AF" },
    { "GP", "GLP", "Guadeloupe", "NA" },
    { "GQ", "GNQ", "Equatorial Guinea", "AF" },
    { "GR", "GRC", "Greece", "EU" },
    { "GS", "SGS", "South Georgia and the South Sandwich Islands", "AN" },
    { "GT", "GTM", "Guatemala", "NA" },  /* 90 */
    { "GU", "GUM", "Guam", "OC" },
    { "GW", "GNB", "Guinea-Bissau", "AF" },
    { "GY", "GUY", "Guyana", "SA" },
    { "HK", "HKG", "Hong Kong", "AS" },
    { "HM", "HMD", "Heard Island and McDonald Islands", "AN" },
    { "HN", "HND", "Honduras", "NA" },
    { "HR", "HRV", "Croatia", "EU" },
    { "HT", "HTI", "Haiti", "NA" },
    { "HU", "HUN", "Hungary", "EU" },
    { "ID", "IDN", "Indonesia", "AS" },  /* 100 */
    { "IE", "IRL", "Ireland", "EU" },
    { "IL", "ISR", "Israel", "AS" },
    { "IN", "IND", "India", "AS" },
    { "IO", "IOT", "British Indian Ocean Territory", "AS" },
    { "IQ", "IRQ", "Iraq", "AS" },
    { "IR", "IRN", "Iran", "AS" },
    { "IS", "ISL", "Iceland", "EU" },
    { "IT", "ITA", "Italy", "EU" },
    { "JM", "JAM", "Jamaica", "NA" },
    { "JO", "JOR", "Jordan", "AS" },  /* 110 */
    { "JP", "JPN", "Japan", "AS" },
    { "KE", "KEN", "Kenya", "AF" },
    { "KG", "KGZ", "Kyrgyzstan", "AS" },
    { "KH", "KHM", "Cambodia", "AS" },
    { "KI", "KIR", "Kiribati", "OC" },
    { "KM", "COM", "Comoros", "AF" },
    { "KN", "KNA", "Saint Kitts and Nevis", "NA" },
    { "KP", "PRK", "North Korea", "AS" },
    { "KR", "KOR", "South Korea", "AS" },
    { "KW", "KWT", "Kuwait", "AS" },  /* 120 */
    { "KY", "CYM", "Cayman Islands", "NA" },
    { "KZ", "KAZ", "Kazakhstan", "AS" },
    { "LA", "LAO", "Laos", "AS" },
    { "LB", "LBN", "Lebanon", "AS" },
    { "LC", "LCA", "Saint Lucia", "NA" },
    { "LI", "LIE", "Liechtenstein", "EU" },
    { "LK", "LKA", "Sri Lanka", "AS" },
    { "LR", "LBR", "Liberia", "AF" },
    { "LS", "LSO", "Lesotho", "AF" },
    { "LT", "LTU", "Lithuania", "EU" },  /* 130 */
    { "LU", "LUX", "Luxembourg", "EU" },
    { "LV", "LVA", "Latvia", "EU" },
    { "LY", "LBY", "Libya", "AF" },
    { "MA", "MAR", "Morocco", "AF" },
    { "MC", "MCO", "Monaco", "EU" },
    { "MD", "MDA", "Moldova", "EU" },
    { "MG", "MDG", "Madagascar", "AF" },
    { "MH", "MHL", "Marshall Islands", "OC" },
    { "MK", "MKD", "North Macedonia", "EU" },
    { "ML", "MLI", "Mali", "AF" },  /* 140 */
    { "MM", "MMR", "Myanmar", "AS" },
    { "MN", "MNG", "Mongolia", "AS" },
    { "MO", "MAC", "Macau", "AS" },
    { "MP", "MNP", "Northern Mariana Islands", "OC" },
    { "MQ", "MTQ", "Martinique", "NA" },
    { "MR", "MRT", "Mauritania", "AF" },
    { "MS", "MSR", "Montserrat", "NA" },
    { "MT", "MLT", "Malta", "EU" },
    { "MU", "MUS", "Mauritius", "AF" },
    { "MV", "MDV", "Maldives", "AS" },  /* 150 */
    { "MW", "MWI", "Malawi", "AF" },
    { "MX", "MEX", "Mexico", "NA" },
    { "MY", "MYS", "Malaysia", "AS" },
    { "MZ", "MOZ", "Mozambique", "AF" },
    { "NA", "NAM", "Namibia", "AF" },
    { "NC", "NCL", "New Caledonia", "OC" },
    { "NE", "NER", "Niger", "AF" },
    { "NF", "NFK", "Norfolk Island", "OC" },
    { "NG", "NGA", "Nigeria", "AF" },
    { "NI", "NIC", "Nicaragua", "NA" },  /* 160 */
    { "NL", "NLD", "Netherlands", "EU" },
    { "NO", "NOR", "Norway", "EU" },
    { "NP", "NPL", "Nepal", "AS" },
    { "NR", "NRU", "Nauru", "OC" },
    { "NU", "NIU", "Niue", "OC" },
    { "NZ", "NZL", "New Zealand", "OC" },
    { "OM", "OMN", "Oman", "AS" },
    { "PA", "PAN", "Panama", "NA" },
    { "PE", "PER", "Peru", "SA" },
    { "PF", "PYF", "French Polynesia", "OC" },  /* 170 */
    { "PG", "PNG", "Papua New Guinea", "OC" },
    { "PH", "PHL", "Philippines", "AS" },
    { "PK", "PAK", "Pakistan", "AS" },
    { "PL", "POL", "Poland", "EU" },
    { "PM", "SPM", "Saint Pierre and Miquelon", "NA" },
    { "PN", "PCN", "Pitcairn Islands", "OC" },
    { "PR", "PRI", "Puerto Rico", "NA" },
    { "PS", "PSE", "Palestine", "AS" },
    { "PT", "PRT", "Portugal", "EU" },
    { "PW", "PLW", "Palau", "OC" },  /* 180 */
    { "PY", "PRY", "Paraguay", "SA" },
    { "QA", "QAT", "Qatar", "AS" },
    { "RE", "REU", "Reunion", "AF" },
    { "RO", "ROU", "Romania", "EU" },
    { "RU", "RUS", "Russia", "EU" },
    { "RW", "RWA", "Rwanda", "AF" },
    { "SA", "SAU", "Saudi Arabia", "AS" },
    { "SB", "SLB", "Solomon Islands", "OC" },
    { "SC", "SYC", "Seychelles", "AF" },
    { "SD", "SDN", "Sudan", "AF" },  /* 190 */
    { "SE", "SWE", "Sweden", "EU" },
    { "SG", "SGP", "Singapore", "AS" },
    { "SH", "SHN", "Saint Helena", "AF" },
    { "SI", "SVN", "Slovenia", "EU" },
    { "SJ", "SJM", "Svalbard and Jan Mayen", "EU" },
    { "SK", "SVK", "Slovakia", "EU" },
    { "SL", "SLE", "Sierra Leone", "AF" },
    { "SM", "SMR", "San Marino", "EU" },
    { "SN", "SEN", "Senegal", "AF" },
    { "SO", "SOM", "Somalia", "AF" },  /* 200 */
    { "SR", "SUR", "Suriname", "SA" },
    { "ST", "STP", "Sao Tome and Principe", "AF" },
    { "SV", "SLV", "El Salvador", "NA" },
    { "SY", "SYR", "Syria", "AS" },
    { "SZ", "SWZ", "Eswatini", "AF" },
    { "TC", "TCA", "Turks and Caicos Islands", "NA" },
    { "TD", "TCD", "Chad", "AF" },
    { "TF", "ATF", "French Southern Territories", "AN" },
    { "TG", "TGO", "Togo", "AF" },
    { "TH", "THA", "Thailand", "AS" },  /* 210 */
    { "TJ", "TJK", "Tajikistan", "AS" },
    { "TK", "TKL", "Tokelau", "OC" },
    { "TM", "TKM", "Turkmenistan", "AS" },
    { "TN", "TUN", "Tunisia", "AF" },
    { "TO", "TON", "Tonga", "OC" },
    { "TL", "TLS", "Timor-Leste", "AS" },
    { "TR", "TUR", "Turkey", "EU" },
    { "TT", "TTO", "Trinidad and Tobago", "NA" },
    { "TV", "TUV", "Tuvalu", "OC" },
    { "TW", "TWN", "Taiwan", "AS" },  /* 220 */
    { "TZ", "TZA", "Tanzania", "AF" },
    { "UA", "UKR", "Ukraine", "EU" },
    { "UG", "UGA", "Uganda", "AF" },
    { "UM", "UMI", "United States Minor Outlying Islands", "OC" },
    { "US", "USA", "United States", "NA" },
    { "UY", "URY", "Uruguay", "SA" },
    { "UZ", "UZB", "Uzbekistan", "AS" },
    { "VA", "VAT", "Vatican City", "EU" },
    { "VC", "VCT", "Saint Vincent and the Grenadines", "NA" },
    { "VE", "VEN", "Venezuela", "SA" },  /* 230 */
    { "VG", "VGB", "British Virgin Islands", "NA" },
    { "VI", "VIR", "U.S. Virgin Islands", "NA" },
    { "VN", "VNM", "Vietnam", "AS" },
    { "VU", "VUT", "Vanuatu", "OC" },
    { "WF", "WLF", "Wallis and Futuna", "OC" },
    { "WS", "WSM", "Samoa", "OC" },
    { "YE", "YEM", "Yemen", "AS" },
    { "YT", "MYT", "Mayotte", "AF" },
    { "RS", "SRB", "Serbia", "EU" },
    { "ZA", "ZAF", "South Africa", "AF" },  /* 240 */
    { "ZM", "ZMB", "Zambia", "AF" },
    { "ME", "MNE", "Montenegro", "EU" },
    { "ZW", "ZWE", "Zimbabwe", "AF" },
    { "A1", "A1", "Anonymous Proxy", "--" },
    { "A2", "A2", "Satellite Provider", "--" },
    { "O1", "O1", "Other", "--" },
    { "AX", "ALA", "Aland Islands", "EU" },
    { "GG", "GGY", "Guernsey", "EU" },
    { "IM", "IMN", "Isle of Man", "EU" },
    { "JE", "JEY", "Jersey", "EU" },  /* 250 */
    { "BL", "BLM", "Saint Barthelemy", "NA" },
    { "MF", "MAF", "Saint Martin", "NA" },
    { "BQ", "BES", "Bonaire, Sint Eustatius and Saba", "NA" },
    { "SS", "SSD", "South Sudan", "AF" },
    { "O1", "O1", "Other", "--" },
};

/* NOT_FROM_ORIGINAL_SOURCE: coduox: self getGeoLocation(type) */
void coduomp_player_cmd_getgeolocation(uint32_t entityNum)
{
    const coduomp_geolocation_country_t *country;
    int countryIndex;

    if (entityNum > ENTITYNUM_NONE) {
        Scr_Error(va("%i is not a valid entity number", (int)entityNum));
        return;
    }
    if (g_entities[entityNum].client == NULL) {
        Scr_Error(va("entity %i is not a player", (int)entityNum));
        return;
    }
    if (Scr_GetNumParam() != 1) {
        Scr_Error("USAGE: self getGeoLocation(<integer>)");
        return;
    }

    countryIndex = coduomp_trap_GetClientCountryIndex((int)entityNum);
    if (countryIndex < 0 ||
        countryIndex >= CODUOMP_GEOLOCATION_COUNTRY_COUNT) {
        countryIndex = 0;
    }
    country = &coduomp_geolocation_countries[countryIndex];

    switch (Scr_GetInt(0)) {
    case CODUOMP_GEOLOCATION_CODE:
        Scr_AddString(country->code);
        return;
    case CODUOMP_GEOLOCATION_CODE3:
        Scr_AddString(country->code3);
        return;
    case CODUOMP_GEOLOCATION_NAME:
        Scr_AddString(country->name);
        return;
    case CODUOMP_GEOLOCATION_CONTINENT:
        Scr_AddString(country->continent);
        return;
    default:
        Scr_AddInt(countryIndex);
        return;
    }
}
