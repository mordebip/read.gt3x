#include <Rcpp.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

using namespace Rcpp;
using namespace std;

namespace patch {
template < typename T > std::string to_string( const T& n ) {
  std::ostringstream stm ;
  stm << n ;
  return stm.str() ;
}
}

// tuomo.a.nieminen@gmail.com
// 2018

// C++ code to parse log records from .gt3x files
// Implementation is based on documentation here: https://github.com/actigraph/GT3X-File-Format


// ----------
// Constants
// ----------

const int N_ACTIVITYCOLUMNS = 3; // accelometer measures in three directions: x,y,z
const int SIGNIF_DIGITS = 3;
const int TIME_UNIT = 100; // hundreth of a second


// The gt3x logrecord types.
enum LogRecordType {
  RECORDTYPE_ACTIVITY         = 0x00, // One second of raw activity samples packed into 12-bit values in YXZ order.
  RECORDTYPE_BATTERY          = 0x02, // Battery voltage in millivolts as a little-endian unsigned short (2 bytes).
  RECORDTYPE_EVENT            = 0x03, // Logging records used for internal debugging.
  RECORDTYPE_VHEART_RATE_BPM  = 0x04, // Heart rate average beats per minute (BPM) as one byte unsigned integer.
  RECORDTYPE_LUX              = 0x05, // Lux value as a little-endian unsigned short (2 bytes).
  RECORDTYPE_METADATA         = 0x06, // Arbitrary metadata content. The first record in every log is contains subject data in JSON format.
  RECORDTYPE_TAG              = 0x07, // 13 Byte Serial, 1 Byte Tx Power, 1 Byte (signed) RSSI
  RECORDTYPE_EPOCH            = 0x09, // EPOCH	60-second epoch data
  RECORDTYPE_HEART_RATE_ANT   = 0x0B, // Heart Rate RR information from ANT+ sensor.
  RECORDTYPE_EPOCH2           = 0x0C, // 60-second epoch data
  RECORDTYPE_CAPSENSE         = 0x0D, // Capacitive sense data
  RECORDTYPE_HEART_RATE_BLE   = 0x0E, // Bluetooth heart rate information (BPM and RR). This is a Bluetooth standard format.
  RECORDTYPE_EPOCH3           = 0x0F, // 60-second epoch data
  RECORDTYPE_EPOCH4           = 0x10, // 60-second epoch data
  RECORDTYPE_PARAMETERS       = 0x15, // Records various configuration parameters and device attributes on initialization.
  RECORDTYPE_SENSOR_SCHEMA    = 0x18, // This record allows dynamic definition of a SENSOR_DATA record format.
  RECORDTYPE_SENSOR_DATA      = 0x19, // This record stores sensor data according to a SENSOR_SCHEMA definition.
  RECORDTYPE_ACTIVITY2        = 0x1A  // One second of raw activity samples as little-endian signed-shorts in XYZ order.
};


// these are needed by decodeFloatParameterValue()
// const double PARAM_FLOAT_MINIMUM = 0.00000011920928955078125;  /* 2^-23 */
const double PARAM_FLOAT_MAXIMUM = 8388608.0;                  /* 2^23  */
const uint32_t PARAM_ENCODED_MINIMUM = 0x00800000;
const uint32_t PARAM_ENCODED_MAXIMUM = 0x007FFFFF;
const uint32_t PARAM_SIGNIFICAND_MASK = 0x00FFFFFFu;
// const int PARAM_EXPONENT_MINIMUM = -128;
// const int PARAM_EXPONENT_MAXIMUM = 127;
const uint32_t PARAM_EXPONENT_MASK = 0xFF000000u;
const int PARAM_EXPONENT_OFFSET = 24;

// -------------
// END Constants


// ----------------
// PARSE PARAMETERS
// ----------------


// Helper for ParseParameters() to decode float valued parameters
double decodeFloatParameterValue(const uint32_t value) {
  double significand;
  double exponent;
  int32_t i32;

  /* handle numbers that are too big */
  if (PARAM_ENCODED_MAXIMUM == value)
    return DBL_MAX;
  else if (PARAM_ENCODED_MINIMUM == value)
    return -DBL_MAX;

  /* extract the exponent */
  i32 = (int32_t) ((value & PARAM_EXPONENT_MASK) >> PARAM_EXPONENT_OFFSET);
  if (0 != (i32 & 0x80))
    i32 = (int32_t)((uint32_t)i32 | 0xFFFFFF00u);
  exponent = (double)i32;

  /* extract the significand */
  i32 = (int32_t)(value & PARAM_SIGNIFICAND_MASK);
  if (0 != (i32 & PARAM_ENCODED_MINIMUM))
    i32 = (Int32)((uint32_t)i32 | 0xFF000000u);
  significand = (double)i32 / PARAM_FLOAT_MAXIMUM;

  /* calculate the floating point value */
  return significand * pow(2.0, exponent);
};

// saves the start time from log.bin parameters and prints all of the parameters (if verbose = true)
// ref: https://github.com/actigraph/GT3X-File-Format/blob/master/LogRecords/Parameters.md
void ParseParameters(ifstream& stream, int bytes, uint32_t& start_time, bool verbose) {
  // The record payload is of variable length consisting of 8-byte key/value pairs.
  int n_params = bytes / 8;
  uint16_t address;
  uint16_t key;
  uint32_t value;
  double decoded_value;

  if(verbose)
    Rcout << "---GT3X PARAMETERS\n";

  for(int i = 0; i < n_params; ++i) {
    stream.read(reinterpret_cast<char*>(&address), 2);
    stream.read(reinterpret_cast<char*>(&key), 2);
    stream.read(reinterpret_cast<char*>(&value), 4);

    if(verbose)
      Rcout << "address: " << address << " key: " << key;

    if(address == 0) {

      // these are floats that must be converted
      if( ( (key == 49) | (key == 51) | (key == 55) | (key == 57) | (key == 58) ) ) {
        decoded_value = decodeFloatParameterValue(value);
        if(verbose)
          Rcout << " value: " << decoded_value << "\n";
      }
      else if(verbose)
        Rcout <<  " value: " << value << "\n";
    }

    else if(address == 1) {
      if(key == 12) { // start time
        start_time = (uint32_t)value;
        if(verbose)
          Rcout << " (start time) ";
      }
      if(verbose)
        Rcout <<  " value: " << value << "\n";
    }
  }
  if(verbose)
    Rcout << "---END PARAMETERS\n\n";
};

// --------------------
// END Parse parameters



// ---------------------------------------------
// Activity parsers for the two possible formats
// ---------------------------------------------



// number of time units passed since start_time for i:th sample in payload
uint32_t createTimeStamp(uint32_t payload_start, int i, int sample_rate, uint32_t start_time) {
  return round( ( (double_t)(payload_start - start_time) + (double_t)i * (1.0 / sample_rate ) ) * TIME_UNIT) ;
}


// Parse second of activity data (type 2) and insert into matrix 'out'
// ref: https://github.com/actigraph/GT3X-File-Format/blob/master/LogRecords/Activity2.md
void ParseActivity2(ifstream& stream, NumericMatrix& activity, IntegerVector& timeStamps, int start, int sample_size, uint32_t payload_start, int sample_rate, uint32_t start_time, bool debug) {
  int16_t item;

  for(int i = 0; i < sample_size; ++i) {
    for (int j = 0; j < N_ACTIVITYCOLUMNS; ++j) {
      stream.read(reinterpret_cast<char*>(&item), 2);
      activity(i + start, j) = item;
    }
    timeStamps(i + start) = createTimeStamp(payload_start, i, sample_rate, start_time);
  }
}


// Parse a second of activity data (type 1) and insert into matrix 'out'
// ref: https://github.com/actigraph/GT3X-File-Format/blob/master/LogRecords/Activity.md
void ParseActivity(ifstream& stream, NumericMatrix& activity, IntegerVector& timeStamps, int start, int sample_size, uint32_t payload_start, int sample_rate, uint32_t start_time, bool debug) {

  bool odd = 0;
  int current = 0;

  for(int i = 0; i < sample_size; ++i) {
    for (int j = 0; j < N_ACTIVITYCOLUMNS; ++j) {
      uint16_t shifter;

      if (!odd) {
        current = stream.get();

        // (0xFF=1111 1111) shifter = 0000 0000 current[1] 0000
        shifter = (uint16_t)((current & 0xFF) << 4);

        current = stream.get();

        // (0xF0=1111 0000) shifter = 0000 0000 current[1] current[2]
        shifter |= (uint16_t)((current & 0xF0) >> 4);
      }
      else {

        // (0x0F=0000 1111) shifter = 0000 current[1] 0000 0000
        shifter = (uint16_t)((current & 0x0F) << 8);

        current = stream.get();
        if (!stream) break;

        // (0xFF=1111 1111) shifter = 0000 current [1] 0000 current[2]
        shifter |= (uint16_t)(current & 0xFF);
      }

      // sign-extension
      if (0 != (shifter & 0x0800))
        shifter |= 0xF000;

      // convert to signed int
      activity(i + start, j) = (int16_t)shifter;
      odd = !odd;
    }
    timeStamps(i + start) = createTimeStamp(payload_start, i, sample_rate, start_time);
  }
}

// ---------------------------------------------
// END Activity parsers


// Imputation

// the data matrix is initialized with zeroes
// to 'impute' zeroes, simply go forward in time stamps
// ImputeZeroes(timeStamps, total_records, n_missing, sample_rate, start_time, debug);
void ImputeZeroes(IntegerVector& timeStamps, int total_records, int sample_size, int sample_rate, uint32_t start_time, bool debug) {

  if(debug)
    Rcout << "imputing " << sample_size << " values at index " << total_records << " \n";

  for(int i = 0; i < sample_size; ++i)
    timeStamps(i + total_records) = createTimeStamp(total_records, i, sample_rate, start_time);
}


// -----------------------
// Helpers for gt3x main parser parseGT3X()
// -----------------------

// Parse the header of a log entry
// ref: https://stackoverflow.com/questions/2974643/reading-in-4-bytes-at-a-time
void ParseHeader(ifstream& stream, uint8_t& type, uint32_t& timestamp, uint16_t& size) {
  stream.read(reinterpret_cast<char*>(&type), 1);
  stream.read(reinterpret_cast<char*>(&timestamp), 4);
  stream.read(reinterpret_cast<char*>(&size), 2);
}

// Perform scaling and rounding to the activity measurements
void scaleAndRoundActivity(NumericMatrix& M, const double scale, int records = -1, const int digits = SIGNIF_DIGITS) {

  if(records == -1)
    records = M.nrow();

  const double digit_multiplier = pow(10, digits);

  for(int j = 0; j < N_ACTIVITYCOLUMNS; ++j)
    for(int i = 0; i < records; i++)
      M(i, j) = round( (M(i, j) / scale) * digit_multiplier ) / digit_multiplier;
}



// Convert byte size to sample size for the two possible activity data formats
int bytes2samplesize(uint8_t& type, uint16_t& bytes) {
  int sample_size = 0;
  if(type == RECORDTYPE_ACTIVITY) {
    sample_size = (bytes * 2) / 9;
  }
  else if(type == RECORDTYPE_ACTIVITY2) {
    sample_size = (bytes / 2) / 3;
  }
  return sample_size;
}

// ----------------
// END helpers



//' Parse activity samples from a GT3X file
//'
//' @param filename (char*) path to a log.bin file inside the unzipped gt3x folder, which contains the activity samples
//' @param max_samples Maximum number of rows to parse. The returned matrix will always contain this number of rows, having zeroes if
//' not data is found.
//' @param scale_factor Scale factor for the activity samples.
//' @param sample_rate sampling rate for activity samples.
//' @param verbose Print the parameters from the log.bin file and other messages?
//' @param impute_zeroes Impute zeros in case there are missingness?
//' @param debug Print information for every activity second
//'
//' @return
//' Returns a matrix with max_samples rows and 4 columns, where the first 3 columns are the acceleration samples and
//' the last column is timestamps in seconds (including 100th of seconds) starting from 00:00:00 1970-01-01 UTC (UNIX time)
//'
// [[Rcpp::export]]
NumericMatrix parseGT3X(const char* filename, const int max_samples, const double scale_factor, const int sample_rate,
                        const bool verbose = false, const bool debug = false, const bool impute_zeroes = false) {
  ifstream GT3Xstream;
  GT3Xstream.open(filename,  std::ios_base::binary);
  // Rcpp::NumericMatrix activityMatrix = Rcpp::no_init(max_samples, N_ACTIVITYCOLUMNS);
  NumericMatrix activityMatrix(max_samples, N_ACTIVITYCOLUMNS);
  IntegerVector timeStamps(max_samples);
  IntegerVector Missingness;

  const uint8_t RECORD_SEPARATOR = 30;

  uint8_t type;
  uint8_t item;
  uint16_t size;
  uint32_t payload_start;
  uint32_t start_time;
  uint32_t expected_payload_start;
  int payload_timediff;
  int total_records = 0;
  int sample_size;

  int chksum;

  if (debug)
      Rcout << "Reading Stream...\n";
  while(GT3Xstream) {

    item = GT3Xstream.get();
    if(!GT3Xstream) break;

    if(item == RECORD_SEPARATOR) {
      ParseHeader(GT3Xstream, type, payload_start, size);
      sample_size = bytes2samplesize(type, size);

      if(sample_size > sample_rate) {
        sample_size = sample_rate;
      }

      // if (debug)
        // Rcout << "Type: " << LogRecordType(type) << " bytes: " << size << " sampleSize:" << sample_size << "\n";

      if(sample_size + total_records > max_samples) {
        Rcout << "CPP parser warning: max_samples reached prematurely\n";
        break;
      }

      if(type == RECORDTYPE_PARAMETERS) {
        ParseParameters(GT3Xstream, size, start_time, verbose);
        expected_payload_start = start_time + 1;
      }

      if( (type == RECORDTYPE_ACTIVITY) | (type == RECORDTYPE_ACTIVITY2) ) {

        payload_timediff = (int)(payload_start - expected_payload_start);

        if(payload_timediff > 0) {
          int n_missing = payload_timediff*sample_rate;
          Missingness[patch::to_string(expected_payload_start)] = n_missing;

          if(impute_zeroes && total_records > 0) {
            ImputeZeroes(timeStamps, total_records, n_missing, sample_rate, start_time, debug);
            total_records += n_missing;
          }
        }

        expected_payload_start = payload_start + 1;

      }

      if ( (type == RECORDTYPE_ACTIVITY) & (sample_size > 0) ) {
        ParseActivity(GT3Xstream, activityMatrix, timeStamps, total_records, sample_size, payload_start, sample_rate, start_time, debug);
        total_records += sample_size;
      }

      else if ( (type == RECORDTYPE_ACTIVITY2) & (sample_size > 0) ) {
        ParseActivity2(GT3Xstream, activityMatrix, timeStamps, total_records, sample_size, payload_start, sample_rate, start_time, debug);
        total_records += sample_size;
      }

      else {
        if (debug)
          Rcout << "Activity Type but no sample size";
        GT3Xstream.seekg(size, std::ios::cur);
      }

      chksum = GT3Xstream.get();


    } else if (std::ios::cur > 1) {
      Rcout << "CPP parser warnng: Stream nro: " << std::ios::cur << ". First item: " << item << " was not a record separator\n";
    }
  }

  if (verbose)
    Rcout << "Total Records: " << total_records << "\n";
  GT3Xstream.close();

  if(verbose)
    Rcout << "Scaling...\n";
  scaleAndRoundActivity(activityMatrix, scale_factor, total_records);

  if(verbose)
    Rcout << "Removing excess rows \n";
  activityMatrix =  activityMatrix(Range(0, total_records - 1), Range(0, N_ACTIVITYCOLUMNS - 1));

  if(verbose)
    Rcout << "Creating dimnames \n";

  colnames(activityMatrix) = CharacterVector::create("X", "Y", "Z");
  activityMatrix.attr("time_index") = timeStamps[Range(0, total_records - 1)];
  activityMatrix.attr("missingness") = Missingness;

  activityMatrix.attr("start_time_log") = start_time;
  activityMatrix.attr("sample_rate") = sample_rate;

  if(verbose)
    Rcout << "CPP returning \n";

  return activityMatrix;

}


//' Parse activity samples from a NHANES-GT3X file
//'
//' @param filename path to a activity.bin file inside the unzipped gt3x
//' folder, which contains the activity samples
//' @param max_samples Maximum number of rows to parse.
//' The returned matrix will always contain this number of rows, having zeroes if
//' not data is found.
//' @param scale_factor Scale factor for the activity samples.
//' @param sample_rate sampling rate for activity samples.
//' @param verbose Print the parameters from the activity.bin file and other messages?
//' @param debug Print information for every activity second
//'
//' @return
//' Returns a matrix with max_samples rows and 3 columns, where the first 3
//' columns are the acceleration samples and
//' the last column is timestamps in seconds (including 100th of seconds)
//' starting from 00:00:00 1970-01-01 UTC (UNIX time)
//'
// [[Rcpp::export]]
NumericMatrix parseActivityBin(const char* filename,
                               const int max_samples,
                               const double scale_factor,
                               const int sample_rate,
                               const bool verbose = false,
                               const bool debug = false) {
  ifstream GT3Xstream;
  GT3Xstream.open(filename,  std::ios_base::binary);
  NumericMatrix activityMatrix(max_samples, N_ACTIVITYCOLUMNS);
  IntegerVector timeStamps(max_samples);

  uint32_t payload_start = 0;
  uint32_t start_time = 0;
  int start = 0;
  int sample_size = max_samples;


  ParseActivity(GT3Xstream, activityMatrix, timeStamps, start, sample_size, payload_start, sample_rate, start_time, debug);
  if (verbose)
    Rcout << "Sample size: " << sample_size << "\n";
  GT3Xstream.close();

  if(verbose)
    Rcout << "Scaling...\n";
  scaleAndRoundActivity(activityMatrix, scale_factor, sample_size);

  colnames(activityMatrix) = CharacterVector::create("X", "Y", "Z");
  activityMatrix.attr("time_index") = timeStamps[Range(0, sample_size - 1)];

  activityMatrix.attr("start_time_log") = start_time;
  activityMatrix.attr("sample_rate") = sample_rate;

  return activityMatrix ;
}


// Parse second of activity data (type 2) and insert into matrix 'out'
// ref: https://github.com/actigraph/NHANES-GT3X-File-Format/blob/master/fileformats/lux.bin.md
void ParseLux(ifstream& stream, NumericVector& luxvec, double LuxScaleFactor, double LuxMaxValue,  int sample_size) {

  // stream.seekg( 0, std::ios::end );
  // uint16_t file_length = stream.tellg() ;
  // stream.seekg( 0, std::ios::beg );
  uint16_t lux ;
  double dbl_lux;

  for(int i = 0; i < sample_size; ++i) {
    stream.read(reinterpret_cast<char*>(&lux), sizeof(uint16_t));

    if (lux < 20.0)
      dbl_lux = 0.0;
    else if (lux >= 65535.0)
      dbl_lux = 0.0 ;
    else
      dbl_lux = min(lux * LuxScaleFactor, LuxMaxValue);

    //round to nearest integer
    dbl_lux = round(dbl_lux);
    luxvec(i) = dbl_lux;
  }

}


//' Parse activity samples from a GT3X file
//'
//' @param filename (char*) path to a log.bin file inside the unzipped gt3x folder, which contains the activity samples
//' @param max_samples Maximum number of rows to parse. The returned matrix will always contain this number of rows, having zeroes if
//' not data is found.
//' @param scale_factor Scale factor for the activity samples.
//' @param max_value Maximum value to truncate
//' @param verbose Print the parameters from the log.bin file and other messages?
//'
//' @return
//' Returns a vector with max_samples eleements
//'
// [[Rcpp::export]]
NumericVector parseLuxBin(const char* filename,
                          const int max_samples,
                          const double scale_factor,
                          const double max_value,
                          const bool verbose = false) {
  ifstream GT3Xstream;
  GT3Xstream.open(filename,  std::ios_base::binary);
  NumericVector luxvec(max_samples);

  int sample_size = max_samples;

  ParseLux(GT3Xstream, luxvec, scale_factor, max_value, sample_size);
  if (verbose)
    Rcout << "Lux Sample size: " << sample_size << "\n";
  GT3Xstream.close();

  return luxvec ;
}
