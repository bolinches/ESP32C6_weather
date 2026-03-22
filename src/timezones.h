#pragma once

// Forward declaration for the logging function defined in main.cpp
void logMsg(String m);

// Using 'inline' is a good practice for functions in headers to prevent multiple definition errors
// if this header were to be included in other .cpp files in the future.
inline String getPosixFromIana(String iana) {
  logMsg("TZ: Browser reports IANA timezone: " + iana);
  
  // North America
  if (iana == "America/New_York" || iana == "America/Toronto" || iana == "America/Montreal" || iana == "America/Detroit") return "EST5EDT,M3.2.0/2,M11.1.0/2";
  if (iana == "America/Chicago" || iana == "America/Winnipeg") return "CST6CDT,M3.2.0/2,M11.1.0/2";
  if (iana == "America/Denver" || iana == "America/Edmonton") return "MST7MDT,M3.2.0/2,M11.1.0/2";
  if (iana == "America/Los_Angeles" || iana == "America/Vancouver" || iana == "America/Tijuana") return "PST8PDT,M3.2.0/2,M11.1.0/2";
  if (iana == "America/Phoenix") return "MST7";
  if (iana == "America/Mexico_City") return "CST6"; // No DST as of 2023
  if (iana == "America/Halifax") return "AST4ADT,M3.2.0,M11.1.0";

  // South America
  if (iana == "America/Sao_Paulo" || iana == "America/Buenos_Aires" || iana == "America/Montevideo") return "<-03>3";
  if (iana == "America/Bogota" || iana == "America/Lima" || iana == "America/Guayaquil") return "<-05>5";
  if (iana == "America/Caracas" || iana == "America/La_Paz" || iana == "America/Santiago") return "<-04>4";
  
  // Europe - Western (WET/GMT: UTC+0, DST+1)
  if (iana == "Europe/London" || iana == "Europe/Dublin" || iana == "Europe/Lisbon") 
    return "GMT0BST,M3.5.0/1,M10.5.0/2";
    
  // Europe - Central (CET: UTC+1, DST+2)
  if (iana == "Europe/Paris" || iana == "Europe/Berlin" || iana == "Europe/Madrid" || 
      iana == "Europe/Rome" || iana == "Europe/Warsaw" || iana == "Europe/Amsterdam" || 
      iana == "Europe/Brussels" || iana == "Europe/Vienna" || iana == "Europe/Zurich" || 
      iana == "Europe/Stockholm" || iana == "Europe/Oslo" || iana == "Europe/Copenhagen" || 
      iana == "Europe/Prague" || iana == "Europe/Budapest" || iana == "Europe/Belgrade" || 
      iana == "Europe/Zagreb" || iana == "Europe/Sarajevo" || iana == "Europe/Bratislava") 
    return "CET-1CEST,M3.5.0,M10.5.0/3";
    
  // Europe - Eastern (EET: UTC+2, DST+3)
  if (iana == "Europe/Athens" || iana == "Europe/Bucharest" || iana == "Europe/Sofia" || 
      iana == "Europe/Kyiv" || iana == "Europe/Kiev" || iana == "Europe/Helsinki" || 
      iana == "Europe/Vilnius" || iana == "Europe/Riga" || iana == "Europe/Tallinn" || 
      iana == "Europe/Chisinau") 
    return "EET-2EEST,M3.5.0/3,M10.5.0/4";
    
  // Europe - Further East (UTC+3, No DST)
  if (iana == "Europe/Moscow" || iana == "Europe/Minsk" || iana == "Europe/Istanbul" || iana == "Asia/Riyadh" || iana == "Asia/Kuwait")
    return "MSK-3";

  // Africa
  if (iana == "Africa/Cairo") return "EET-2EEST,M4.5.5/0,M10.5.4/23";
  if (iana == "Africa/Johannesburg") return "SAST-2";
  if (iana == "Africa/Lagos") return "WAT-1";
  if (iana == "Africa/Nairobi" || iana == "Africa/Addis_Ababa") return "EAT-3";
  if (iana == "Africa/Casablanca") return "<+01>-1";

  // Asia / Oceania
  if (iana == "Asia/Tokyo") return "JST-9";
  if (iana == "Asia/Shanghai" || iana == "Asia/Hong_Kong" || iana == "Asia/Singapore" || iana == "Asia/Taipei" || iana == "Asia/Manila" || iana == "Asia/Kuala_Lumpur") return "CST-8";
  if (iana == "Asia/Seoul") return "KST-9";
  if (iana == "Asia/Bangkok" || iana == "Asia/Jakarta" || iana == "Asia/Ho_Chi_Minh") return "ICT-7";
  if (iana == "Asia/Karachi" || iana == "Asia/Tashkent") return "PKT-5";
  if (iana == "Asia/Dhaka" || iana == "Asia/Almaty") return "<-06>-6";
  if (iana == "Australia/Sydney") return "AEST-10AEDT,M10.1.0,M4.1.0/3";
  if (iana == "Australia/Brisbane") return "AEST-10";
  if (iana == "Pacific/Auckland") return "NZST-12NZDT,M9.5.0,M4.1.0/3";
  if (iana == "Pacific/Honolulu") return "HST10";
  if (iana == "Pacific/Fiji") return "<-12>-12";
  if (iana == "Asia/Dubai") return "GST-4";
  if (iana == "Asia/Kolkata") return "IST-5:30";
  logMsg("TZ: No match in IANA table. Defaulting to UTC.");
  return "UTC0";
}