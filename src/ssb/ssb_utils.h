#include <iostream>
#include <fstream>
#include <string>

/*#include <cuda.h>*/
/*#include <cub/util_allocator.cuh>*/

using namespace std;

// #define SF 200

#if SF == 20
#define DATA_DIR  "/mnt/hdd2/ssb-dbgen-20/transformed/"
#define LO_LEN 119994608
#define P_LEN 1000000 
#define S_LEN 40000
#define C_LEN 600000
#define D_LEN 2556
#elif SF == 40
#define DATA_DIR "/mnt/hdd2/ssb-dbgen-40/transformed/"
#define LO_LEN 240012290
#define P_LEN 1200000 
#define S_LEN 80000
#define C_LEN 1200000
#define D_LEN 2556
#elif SF == 60
#define DATA_DIR "/mnt/hdd2/ssb-dbgen-60/transformed/"
#define LO_LEN 360011594
#define P_LEN 1200000 
#define S_LEN 120000
#define C_LEN 1800000
#define D_LEN 2556
#elif SF == 80
#define DATA_DIR "/mnt/hdd3/ssb-dbgen-80/transformed/"
#define LO_LEN 480025129
#define P_LEN 1400000 
#define S_LEN 160000
#define C_LEN 2400000
#define D_LEN 2556
#elif SF == 100
#define DATA_DIR "/mnt/hdd3/ssb-dbgen-100/transformed/"
#define LO_LEN 600037902
#define P_LEN 1400000 
#define S_LEN 200000
#define C_LEN 3000000
#define D_LEN 2556
#elif SF == 120
#define DATA_DIR "/mnt/hdd3/ssb-dbgen-120/transformed/"
#define LO_LEN 720040849
#define P_LEN 1400000 
#define S_LEN 240000
#define C_LEN 3600000
#define D_LEN 2556
#elif SF == 140
#define DATA_DIR "/mnt/hdd3/ssb-dbgen-140/transformed/"
#define LO_LEN 840042983
#define P_LEN 1600000 
#define S_LEN 280000
#define C_LEN 4200000
#define D_LEN 2556
#elif SF == 160
#define DATA_DIR "/mnt/hdd3/ssb-dbgen-160/transformed/"
#define LO_LEN 960017389
#define P_LEN 1600000 
#define S_LEN 320000
#define C_LEN 4800000
#define D_LEN 2556
#elif SF == 180
#define DATA_DIR "/mnt/hdd3/ssb-dbgen-180/transformed/"
#define LO_LEN 1080017552
#define P_LEN 1600000 
#define S_LEN 360000
#define C_LEN 5400000
#define D_LEN 2556
#elif SF == 200
#define DATA_DIR "/mnt/hdd3/ssb-dbgen-200/transformed/"
#define LO_LEN 1200018434
#define P_LEN 1600000 
#define S_LEN 400000
#define C_LEN 6000000
#define D_LEN 2556
#endif



int index_of(string* arr, int len, string val) {
  for (int i=0; i<len; i++)
    if (arr[i] == val)
      return i;

  return -1;
}

string lookup(string col_name) {
  string lineorder[] = { "lo_orderkey", "lo_linenumber", "lo_custkey", "lo_partkey", "lo_suppkey", "lo_orderdate", "lo_orderpriority", "lo_shippriority", "lo_quantity", "lo_extendedprice", "lo_ordtotalprice", "lo_discount", "lo_revenue", "lo_supplycost", "lo_tax", "lo_commitdate", "lo_shipmode"};
  string part[] = {"p_partkey", "p_name", "p_mfgr", "p_category", "p_brand1", "p_color", "p_type", "p_size", "p_container"};
  string supplier[] = {"s_suppkey", "s_name", "s_address", "s_city", "s_nation", "s_region", "s_phone"};
  string customer[] = {"c_custkey", "c_name", "c_address", "c_city", "c_nation", "c_region", "c_phone", "c_mktsegment"};
  string date[] = {"d_datekey", "d_date", "d_dayofweek", "d_month", "d_year", "d_yearmonthnum", "d_yearmonth", "d_daynuminweek", "d_daynuminmonth", "d_daynuminyear", "d_sellingseason", "d_lastdayinweekfl", "d_lastdayinmonthfl", "d_holidayfl", "d_weekdayfl"};

  if (col_name[0] == 'l') {
    int index = index_of(lineorder, 17, col_name);
    return "LINEORDERSORT" + to_string(index);
  } else if (col_name[0] == 's') {
    int index = index_of(supplier, 7, col_name);
    return "SUPPLIER" + to_string(index);
  } else if (col_name[0] == 'c') {
    int index = index_of(customer, 8, col_name);
    return "CUSTOMER" + to_string(index);
  } else if (col_name[0] == 'p') {
    int index = index_of(part, 9, col_name);
    return "PART" + to_string(index);
  } else if (col_name[0] == 'd') {
    int index = index_of(date, 15, col_name);
    return "DDATE" + to_string(index);
  }

  return "";
}

template<typename T>
T* loadColumn(string col_name, int num_entries) {
  T* h_col = new T[num_entries];
  string filename = DATA_DIR + lookup(col_name);
  ifstream colData (filename.c_str(), ios::in | ios::binary);
  if (!colData) {
    return NULL;
  }

  colData.read((char*)h_col, num_entries * sizeof(T));
  return h_col;
}

template<typename T>
int storeColumn(string col_name, int num_entries, int* h_col) {
  string filename = DATA_DIR + lookup(col_name);
  ofstream colData (filename.c_str(), ios::out | ios::binary);
  if (!colData) {
    return -1;
  }

  colData.write((char*)h_col, num_entries * sizeof(T));
  return 0;
}

/*int main() {*/
  //int *h_col = new int[10];
  //for (int i=0; i<10; i++) h_col[i] = i;
  //storeColumn<int>("test", 10, h_col);
  //int *l_col = loadColumn<int>("test", 10);
  //for (int i=0; i<10; i++) cout << l_col[i] << " ";
  //cout << endl;
  //return 0;
/*}*/
