/*****************************************************************************
* | File      	:   LCD_1in28_LVGL_test.c
* | Author      :   Waveshare team
* | Function    :   1.28inch LCD  test demo
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2023-12-23
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/

#include "LVGL_example.h" 
#include "core/lv_obj.h"
#include "misc/lv_area.h"

// LVGL
static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf0[DISP_HOR_RES * DISP_VER_RES/2 ];
static lv_color_t buf1[DISP_HOR_RES * DISP_VER_RES/2 ];
static lv_disp_drv_t disp_drv;

static lv_indev_drv_t indev_ts;
static lv_indev_drv_t indev_en;
static lv_group_t *group;
static lv_obj_t *label_imu;

// Input Device 
static int16_t encoder_diff;
static lv_indev_state_t encoder_act;

static uint16_t ts_x;
static uint16_t ts_y;
static lv_indev_state_t ts_act;

// Timer 
static struct repeating_timer lvgl_timer;
static struct repeating_timer imu_diff_timer;


static void disp_flush_cb(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * color_p);
static void touch_callback(uint gpio, uint32_t events);
static void ts_read_cb(lv_indev_drv_t * drv, lv_indev_data_t*data);
static void get_diff_data(void);
static void encoder_read_cb(lv_indev_drv_t * drv, lv_indev_data_t*data);
static void dma_handler(void);
static bool repeating_lvgl_timer_callback(struct repeating_timer *t); 
static bool repeating_imu_diff_timer_callback(struct repeating_timer *t);


lv_group_t *LVGL_get_group(){
	return group;
}

/********************************************************************************
function:	Initializes LVGL and enbable timers IRQ and DMA IRQ
parameter:
********************************************************************************/
void LVGL_Init(void)
{
    // /*1.Init Timer*/ 
    //add_repeating_timer_ms(5,   repeating_lvgl_timer_callback,            NULL, &lvgl_timer);
    //add_repeating_timer_ms(50, repeating_imu_diff_timer_callback,        NULL, &imu_diff_timer);
    
    // /*2.Init LVGL core*/
    lv_init();
   //printf("lv_init 2 end\n");

    // /*3.Init LVGL display*/
    lv_disp_draw_buf_init(&disp_buf, buf0, buf1, DISP_HOR_RES * DISP_VER_RES / 2); 
    lv_disp_drv_init(&disp_drv);    
    //printf("lv_init 3.1\n");
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &disp_buf;        
    //printf("lv_init 3.2\n");
    disp_drv.hor_res = DISP_HOR_RES;
    disp_drv.ver_res = DISP_VER_RES;
    //printf("lv_init 3.3\n");
    lv_disp_t *disp= lv_disp_drv_register(&disp_drv);   
    //printf("lv_init 3 end\n");

#if INPUTDEV_TS
    // /*4.Init touch screen as input device*/ 
    lv_indev_drv_init(&indev_ts); 
    indev_ts.type = LV_INDEV_TYPE_POINTER;    
    indev_ts.read_cb = ts_read_cb;            
    lv_indev_t * ts_indev = lv_indev_drv_register(&indev_ts);
    //Enable touch IRQ
    DEV_IRQ_SET(Touch_INT_PIN, GPIO_IRQ_EDGE_RISE, &touch_callback);
#endif
    printf("lv_init 4 end\n");

    // /*5.Init imu as input device*/
#ifdef USEQMI
    lv_indev_drv_init(&indev_en);   
    indev_en.type = LV_INDEV_TYPE_ENCODER;  
    indev_en.read_cb = encoder_read_cb;         
    lv_indev_t * encoder_indev = lv_indev_drv_register(&indev_en);
    group = lv_group_create();
    lv_indev_set_group(encoder_indev, group);
    printf("lv_init 5 end\n");
#endif

    // /6.Init DMA for transmit color data from memory to SPI
    dma_channel_set_irq0_enabled(dma_tx, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    printf("lv_init 6 end\n");


    add_repeating_timer_ms(5,   repeating_lvgl_timer_callback,            NULL, &lvgl_timer);
}





/********************************************************************************
function:	Refresh image by transferring the color data to the SPI bus by DMA
parameter:
********************************************************************************/
static void disp_flush_cb(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * color_p)
{

    LCD_1IN28_SetWindows(area->x1, area->y1, area->x2 , area->y2);
    dma_channel_configure(dma_tx,
                          &c,
                          &spi_get_hw(LCD_SPI_PORT)->dr, 
                          color_p, // read address
                          ((area->x2 + 1 - area-> x1)*(area->y2 + 1 - area -> y1))*2,
                          true);
}


/********************************************************************************
function:   Touch interrupt handler
parameter:
********************************************************************************/
static void touch_callback(uint gpio, uint32_t events)
{
    if (gpio == Touch_INT_PIN)
    {
        CST816S_Get_Point();
        ts_x = Touch_CTS816.x_point;
        ts_y = Touch_CTS816.y_point;
        ts_act = LV_INDEV_STATE_PRESSED;
    }
}

/********************************************************************************
function:   Update touch screen input device status
parameter:
********************************************************************************/
static void ts_read_cb(lv_indev_drv_t * drv, lv_indev_data_t*data)
{
    data->point.x = ts_x;
    data->point.y = ts_y; 
    data->state = ts_act;
    ts_act = LV_INDEV_STATE_RELEASED;
}



/********************************************************************************
function:	Update encoder input device status
parameter:
********************************************************************************/
static void encoder_read_cb(lv_indev_drv_t * drv, lv_indev_data_t*data)
{
    data->enc_diff = encoder_diff;
    data->state    = encoder_act; 
    /* encoder_diff = 0; */
}

/********************************************************************************
function:   Indicate ready with the flushing when DMA complete transmission
parameter:
********************************************************************************/
static void dma_handler(void)
{
    if (dma_channel_get_irq0_status(dma_tx)) {
        dma_channel_acknowledge_irq0(dma_tx);
        lv_disp_flush_ready(&disp_drv);         /* Indicate you are ready with the flushing*/
    }
}


/********************************************************************************
function:   Report the elapsed time to LVGL each 5ms
parameter:
********************************************************************************/
static bool repeating_lvgl_timer_callback(struct repeating_timer *t) 
{
    lv_tick_inc(5);
    return true;
}


/********************************************************************************
function:   Sample IMU data
parameter:
********************************************************************************/
static void get_diff_data(void)
{
    static int but_flag = 1;

    int i;
    float acc[3], gyro[3];
    unsigned int tim_count = 0;
    float ud_diff = 0;
    float lr_diff = 0;

    float offset = 450;
    float offset_x = 450;

    for( i = 0;i < 3;i ++)
    {
      QMI8658_read_xyz(acc, gyro, &tim_count);
      ud_diff += gyro[1];
      lr_diff += gyro[0];
    }
    ud_diff = ud_diff / 3;
    lr_diff = lr_diff / 3;

    //up or down
    if((ud_diff > offset) && (ud_diff > 0) && (but_flag == 1))
    {
        encoder_diff -= 2;
        but_flag = 0;
    }
    else if((ud_diff < (0-offset)) && (ud_diff < 0) && (but_flag == 1))
    {
        encoder_diff += 2;
        but_flag = 0;
    }
    else
    {
        encoder_diff = 0;
        but_flag = 1;
    }

    //left
    if((lr_diff > offset_x))
        encoder_act = LV_INDEV_STATE_PRESSED;
    else
        encoder_act = LV_INDEV_STATE_RELEASED;
}

/********************************************************************************
function:   Trigger IMU data sampling each 50ms
parameter:
********************************************************************************/
static bool repeating_imu_diff_timer_callback(struct repeating_timer *t)
{
    get_diff_data();
    return true;
}

