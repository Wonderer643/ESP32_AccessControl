#include <stdint.h>

#ifndef _DS_1302_H
#define _DS_1302_H

class Ds1302
{
	public:
        typedef struct {
            uint8_t year;
            uint8_t month;
            uint8_t day;
            uint8_t hour;
            uint8_t minute;
            uint8_t second;
            uint8_t dow;
        } DateTime;
		
	
        Ds1302(uint8_t pin_ena, uint8_t pin_clk, uint8_t pin_dat);
		
        void init();		
		void setDateTime(DateTime* dt);
		void getDateTime(DateTime* dt);
		void DS1302_clock_burst_read( uint8_t *p);		
		void DS1302_clock_burst_write( uint8_t *p);
		uint8_t DS1302_read(int address);
		void DS1302_write( int address, uint8_t data);
        bool isHalted();
		
	private:	
		typedef struct ds1302_struct
		{
		  uint8_t Seconds:4;      // low decimal digit 0-9
		  uint8_t Seconds10:3;    // high decimal digit 0-5
		  uint8_t CH:1;           // CH = Clock Halt
		  uint8_t Minutes:4;
		  uint8_t Minutes10:3;
		  uint8_t reserved1:1;
		  union
		  {
			struct
			{
			  uint8_t Hour:4;
			  uint8_t Hour10:2;
			  uint8_t reserved2:1;
			  uint8_t hour_12_24:1; // 0 for 24 hour format
			} h24;
			struct
			{
			  uint8_t Hour:4;
			  uint8_t Hour10:1;
			  uint8_t AM_PM:1;      // 0 for AM, 1 for PM
			  uint8_t reserved2:1;
			  uint8_t hour_12_24:1; // 1 for 12 hour format
			} h12;
		  };
		  uint8_t Date:4;           // Day of month, 1 = first day
		  uint8_t Date10:2;
		  uint8_t reserved3:2;
		  uint8_t Month:4;          // Month, 1 = January
		  uint8_t Month10:1;
		  uint8_t reserved4:3;
		  uint8_t Day:3;            // Day of week, 1 = first day (any day)
		  uint8_t reserved5:5;
		  uint8_t Year:4;           // Year, 0 = year 2000
		  uint8_t Year10:4;
		  uint8_t reserved6:7;
		  uint8_t WP:1;             // WP = Write Protect
		};	
		
        uint8_t DS1302_CE_PIN;
        uint8_t DS1302_SCLK_PIN;
        uint8_t DS1302_IO_PIN;	
		
		void _DS1302_start( void);
		void _DS1302_stop(void);
		uint8_t _DS1302_toggleread( void);
		void _DS1302_togglewrite( uint8_t data, uint8_t release);
		
	
	
};
#endif // _DS_1302_H
