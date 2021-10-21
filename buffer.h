#pragma once
#ifndef BUFFER_H
#define BUFFER_H

#include <iostream>
#include <map>
#include <unordered_map>
#include "SSD_parameter.h"

using std::string;
using std::map;
using std::vector;
using std::unordered_map;
using std::multimap;

//定義LPN裡面每個sector的資料型態
typedef struct sector sector;
struct sector
{
    //sector的號碼為多少
    long long sector_number;

    //sector的write和read次數
    //這個是用不到的，只是方便觀察一些東西
    int write_count;
    int read_count;

    //sector的狀態，例如clean, dirty, empty, not_exist_read
    char status;
};

//定義buffer裡面LPN的結構
class Logical_page
{
private:

public:
    //LPN的每個sector資訊
    sector sector[sectors_in_a_page];

    //用來判斷每個sector是否已經存在
    bool sector_flag[sectors_in_a_page];

    //LPN的號碼
    long long LPN;

    //LPN的write和read次數
    long write_count;
    long read_count;

    //LPN的sector個數
    int sector_count;

    //這個用不到
    int write_sector_count;

    //LPN上一次被存取的時間
    long long time_access;

    //LPN的weight
    double score;

    //LPN的狀態，例如clean, dirty
    char status;
};

//給buffer在做evict和victim selection時
//裡面max_heap用的方法宣告
class Page_compare
{
public:
    bool operator()(const Logical_page& p1, const Logical_page& p2);
};

//定義buffer裡面的結構
class Buffer
{
private:

public:
    //裡用hash map來管理buffer裡面的每個LPN
    unordered_map<long long, Logical_page> buffer;

    //在buffer evict和victim selection時
    //最少丟幾個LPN的個數
    int least_buffer_evict_count;

    //紀錄當前buffer使用了多少size
    int current_buffer_size;    

    //紀錄當前系統的時間
    long long time_current;        

    //這四個是計算sector在buffer裡面被hit或者miss的次數
    //這些是測試用，是可以拿掉的
    long long write_hit_count;
    long long write_miss_count;
    long long read_hit_count;
    long long read_miss_count;  

    void configuration();

    void write(long long LSN_start, long long LSN_end);    
    void read(long long LSN_start, long long LSN_end);    

    void evict(vector<Logical_page>& victim_LPN_list, int request_size);
    void clean_sector_discard(vector<Logical_page>& victim_LPN_list);
    void victim_selection(vector<Logical_page>& victim_LPN_list, int required_size);

    bool full(int request_size);
    int LPN_classification(Logical_page& victim_LPN);    
    void dynamic_adaption(vector<Logical_page>& victim_LPN_list, vector<Logical_page>& hotRead_hotWrite_list,
        vector<Logical_page>& hotRead_coldWrite_list, vector<Logical_page>& coldRead_hotWrite_list, vector<Logical_page>& coldRead_coldWrite_list);

    void mix(vector<Logical_page>& hot_list, vector<Logical_page>& mixed_PPN);
    void rmw(vector<Logical_page>& cold_list, vector<Logical_page>& non_mixed_PPN);
};

extern Buffer buffer;
extern long long request_number;

void Request_management(string trace[]);
void Initialize_Page(Logical_page& trace_page, sector& trace_sector);
void victim_page_list_write(vector<Logical_page>& victim_page_list);
void buffer_full_handler(int SIZE);

void quick_sort(vector<Logical_page>& victim_LPN_list, int front, int end);
int partition(vector<Logical_page>& victim_LPN_list, int front, int end);

#endif // !BUFFER_H