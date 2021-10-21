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

//�w�qLPN�̭��C��sector����ƫ��A
typedef struct sector sector;
struct sector
{
    //sector�����X���h��
    long long sector_number;

    //sector��write�Mread����
    //�o�ӬO�Τ��쪺�A�u�O��K�[��@�ǪF��
    int write_count;
    int read_count;

    //sector�����A�A�Ҧpclean, dirty, empty, not_exist_read
    char status;
};

//�w�qbuffer�̭�LPN�����c
class Logical_page
{
private:

public:
    //LPN���C��sector��T
    sector sector[sectors_in_a_page];

    //�ΨӧP�_�C��sector�O�_�w�g�s�b
    bool sector_flag[sectors_in_a_page];

    //LPN�����X
    long long LPN;

    //LPN��write�Mread����
    long write_count;
    long read_count;

    //LPN��sector�Ӽ�
    int sector_count;

    //�o�ӥΤ���
    int write_sector_count;

    //LPN�W�@���Q�s�����ɶ�
    long long time_access;

    //LPN��weight
    double score;

    //LPN�����A�A�Ҧpclean, dirty
    char status;
};

//��buffer�b��evict�Mvictim selection��
//�̭�max_heap�Ϊ���k�ŧi
class Page_compare
{
public:
    bool operator()(const Logical_page& p1, const Logical_page& p2);
};

//�w�qbuffer�̭������c
class Buffer
{
private:

public:
    //�̥�hash map�Ӻ޲zbuffer�̭����C��LPN
    unordered_map<long long, Logical_page> buffer;

    //�bbuffer evict�Mvictim selection��
    //�̤֥�X��LPN���Ӽ�
    int least_buffer_evict_count;

    //������ebuffer�ϥΤF�h��size
    int current_buffer_size;    

    //������e�t�Ϊ��ɶ�
    long long time_current;        

    //�o�|�ӬO�p��sector�bbuffer�̭��Qhit�Ϊ�miss������
    //�o�ǬO���եΡA�O�i�H������
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