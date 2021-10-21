//My_method_notExistRead3
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <time.h>
#include "buffer.h"
#include "flash.h"
#include "mapping.h"

using std::cout; using std::cerr;
using std::endl; using std::string;
using std::ifstream; using std::vector;
using std::map; using std::ofstream;
using std::stoi; using std::stoll;

enum category { tASU, tLBA, tSIZE, tTYPE, tTIME };
enum sector_TYPE { READ, WRITE, sEMPTY };
enum STATUS { EMPTY, FREE, ACTIVE, VALID, INVALID, PARTIAL_INVALID, FULL };

long long request_number = 0;
long long block_number = 1111111;

Buffer buffer;
Flash_memory flash_memory;
Mapping_table mapping_table;

int main(int argc, char* argv[])
{
    clock_t start_time, end_time;
    long long cnt = 0;
    int i;
    string i_file = "hm_1.txt";

    //讀取輸入參數
    if (argc >= 3)
    {
        i_file = argv[1];
        block_number = strtoll(argv[2], NULL, 10);
    }
    //手動輸入參數
    else
    {
        printf("請輸入「trace檔名」和「最多可用block個數」\n(例如： exch_0.txt 1111111)： \n");
        std::cin >> i_file >> block_number;
    }

    printf("%s \n", i_file.c_str());
    
    //buffer初始化配置
    buffer.configuration();    
    printf("buffer_size = %dMB, page_size = %dKB, a = %.2f \nleast_evict_count = %d, max_block_number = %lld\n", \
        max_buffer_size / (1024 * 1024), page_size / 1024, a, buffer.least_buffer_evict_count, block_number); 

    //flash_memory初始化配置
    flash_memory.configuration();
    
    //mapping_table初始化配置
    mapping_table.current_table_size = 0;       

    //輸入檔案
    string filename("C:\\Users\\user\\Desktop\\trace\\" + i_file);    
    ifstream input_file(filename);
    if (!input_file.is_open())
    {
        cerr << "Could not open the file - '" << filename << "'" << endl;
        system("pause");
        exit(0);
    }

    //輸出檔案位置
    string o_file(i_file);
    string output_filename("C:\\Users\\user\\Desktop\\trace\\My method\\TestCode9_16KB_64MB\\" + o_file); //buffer size

    //記錄程式開始執行的時間
    start_time = clock();

    //開始讀取trace檔案，一列一列讀取
    string line;
    while (getline(input_file, line))
    {
        ++request_number;
        if (request_number % 20000 == 0)
        {
            printf("request : %lld \n", request_number);
        }
        //cout << line << endl;

        //把request以逗點分割，然後一項一項存入token裡面
        //例如：假設request = 0,0,16384,w,0.000000
        //然後token[5] ={0,0,16384,w,0.0.000000}
        size_t pos = 0;
        string token[5];
        string delimiter = ",";
        for (i = 0; (pos = line.find(delimiter)) != string::npos; ++i)
        {
            token[i] = line.substr(0, pos);
            line.erase(0, pos + delimiter.length());
        }
        token[4] = line;
        
        //debug用，停在第request_number個request
        if (request_number == 200354)
        {
            printf("request : %lld\n", request_number);
        }
        
        Request_management(token);
    }

    //紀錄程式結束時間
    end_time = clock();

    ofstream output_file(output_filename);
    output_file << i_file << "\n";
    output_file << max_buffer_size / (1024 * 1024) << "MB, page_size = " << page_size / 1024 << " KB" << " a = " << a << " evict_count = " << buffer.least_buffer_evict_count << "\n";
    output_file << "write count = " << flash_memory.write_count << "\n";
    output_file << "read count = " << flash_memory.read_count << "\n"; 
    output_file << "rmw read count = " << flash_memory.rmw_read_count << "\n";
    output_file << "flash + rmw read count = " << flash_memory.rmw_read_count + flash_memory.read_count << "\n";
    output_file << "total read count = " << flash_memory.read_count + flash_memory.rmw_read_count << "\n";
    output_file << "migration count = " << flash_memory.migration_count << "\n";
    output_file << "erase count = " << flash_memory.erase_count << "\n";

    output_file << "write hit ratio = " << ((double)buffer.write_hit_count / (buffer.write_hit_count + buffer.write_miss_count)) * 100 << "\n";
    output_file << "read hit ratio = " << ((double)buffer.read_hit_count / (buffer.read_hit_count + buffer.read_miss_count)) * 100 << "\n";   

    output_file << "mapping table size = " << mapping_table.current_table_size << "\n";
    output_file << "mapping table entry = " << mapping_table.L2P.size() << "\n";

    output_file << "used block number = " << flash_memory.used_block_list.size() << "\n";
    output_file << "time = " << (double)(end_time - start_time) / (CLOCKS_PER_SEC * 60) << " min\n";
    output_file.close();

    printf("\nAnalysis completed ! \n\n");
    printf("%s \n", i_file.c_str());
    printf("buffer_size = %dMB, page_size = %dKB, a = %.2f, evict_count = %d\n", max_buffer_size / (1024 * 1024), page_size / 1024, a, buffer.least_buffer_evict_count);
    printf("write count = %lld \n", flash_memory.write_count);
    printf("read count = %lld \n", flash_memory.read_count);    
    printf("rmw read count = %lld \n", flash_memory.rmw_read_count);
    printf("flash + rmw read count = %lld \n", flash_memory.read_count + flash_memory.rmw_read_count);
    printf("total read count = %lld \n", flash_memory.read_count + flash_memory.rmw_read_count);
    printf("migration count = %lld \n", flash_memory.migration_count);
    printf("erase count = %lld \n", flash_memory.erase_count);

    printf("write hit ratio = %lf \n", ((double)buffer.write_hit_count / (buffer.write_hit_count + buffer.write_miss_count)) * 100);
    printf("read hit ratio = %lf \n", ((double)buffer.read_hit_count / (buffer.read_hit_count + buffer.read_miss_count)) * 100);   

    printf("mapping table size = %lld \n", mapping_table.current_table_size);
    printf("mapping table entry = %d \n", mapping_table.L2P.size());    
        
    printf("used block number = %d \n", flash_memory.used_block_list.size());
    printf("\ntime : %lf min\n", (double)(end_time - start_time) / (CLOCKS_PER_SEC * 60));

    input_file.close();
    system("pause");
    return 0;
}