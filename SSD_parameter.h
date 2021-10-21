#pragma once
#ifndef SSD_PARAMETER_H
#define SSD_PARAMETER_H

#define page_size ((16)*(1024))
#define sector_size ((4)*(1024))
#define sectors_in_a_page (page_size/sector_size)
#define max_buffer_size ((64)*(1024)*(1024))
//#define max_buffer_size ((16)*(1024))
#define GC_threshold 0.85
#define pages_in_a_block 128

#define flash_write_latency (600)
#define flash_read_latency (50)
#define flash_erase_latency (3000)
#define a (0.91)
//#define a (flash_write_latency/(flash_read_latency+flash_write_latency))

#endif

