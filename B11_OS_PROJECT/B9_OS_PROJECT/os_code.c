#include "LPC17xx.h"
#include <stdio.h>
#include <stdint.h>

/* ================= LCD ================= */
#define RS (1<<25)
#define RW (1<<26)
#define EN (1<<28)
#define LCD_DATA_MASK 0x00F00000

/* ================= IR SENSOR ================= */
#define IR_PIN (1<<10)   /* P2.10 */

/* ================= BUZZER & LED ================= */
#define BUZZER_PIN (1<<22)   /* P0.22 */
#define LED_PIN    (1<<18)   /* P1.18 (on-board LED) */

/* ================= MAX30102 ================= */
#define MAX30102_ADDR 0x57

/* ================= STATES ================= */
typedef enum
{
    STATE_NO_PATIENT = 0,
    STATE_WELCOME,
    STATE_PLACE_FINGER,
    STATE_MEASURE
} system_state_t;

system_state_t state = STATE_NO_PATIENT;

/* ================= DELAY ================= */
void delay_ms(uint32_t ms)
{
    uint32_t i,j;
    for(i=0;i<ms;i++)
        for(j=0;j<6000;j++);
}

/* ================= LCD ================= */
void LCD_Enable(void)
{
    LPC_GPIO4->FIOSET = EN;
    delay_ms(1);
    LPC_GPIO4->FIOCLR = EN;
}

void LCD_SendNibble(uint8_t n)
{
    LPC_GPIO1->FIOCLR = LCD_DATA_MASK;
    LPC_GPIO1->FIOSET = (n & 0x0F) << 20;
    LCD_Enable();
}

void LCD_Command(uint8_t c)
{
    LPC_GPIO3->FIOCLR = RS | RW;
    LCD_SendNibble(c >> 4);
    LCD_SendNibble(c);
    delay_ms(2);
}

void LCD_Data(uint8_t d)
{
    LPC_GPIO3->FIOSET = RS;
    LPC_GPIO3->FIOCLR = RW;
    LCD_SendNibble(d >> 4);
    LCD_SendNibble(d);
}

void LCD_PrintFixed(const char *s)
{
    int i;
    for(i=0;i<16;i++)
        LCD_Data(s[i] ? s[i] : ' ');
}

void LCD_ClearAndPrint(const char *line1, const char *line2)
{
    LCD_Command(0x01);
    LCD_Command(0x80);
    LCD_PrintFixed(line1);
    if(line2)
    {
        LCD_Command(0xC0);
        LCD_PrintFixed(line2);
    }
}

void LCD_Init(void)
{
    LPC_GPIO1->FIODIR |= LCD_DATA_MASK;
    LPC_GPIO3->FIODIR |= RS | RW;
    LPC_GPIO4->FIODIR |= EN;

    delay_ms(40);
    LCD_SendNibble(0x03); delay_ms(5);
    LCD_SendNibble(0x03); delay_ms(5);
    LCD_SendNibble(0x03); delay_ms(5);
    LCD_SendNibble(0x02);

    LCD_Command(0x28);
    LCD_Command(0x0C);
    LCD_Command(0x06);
    LCD_Command(0x01);
}

/* ================= I2C ================= */
void I2C0_Init(void)
{
    LPC_SC->PCONP |= (1<<7);                 /* Power I2C0 */
    LPC_PINCON->PINSEL1 |= (1<<22)|(1<<24); /* SDA0, SCL0 */
    LPC_I2C0->I2SCLH = 60;
    LPC_I2C0->I2SCLL = 60;
    LPC_I2C0->I2CONSET = (1<<6);             /* Enable I2C */
}

void I2C_Start(void)
{
    LPC_I2C0->I2CONSET = (1<<5);
    while(!(LPC_I2C0->I2CONSET & (1<<3)));
    LPC_I2C0->I2CONCLR = (1<<5);
}

void I2C_Stop(void)
{
    LPC_I2C0->I2CONSET = (1<<4);
    LPC_I2C0->I2CONCLR = (1<<3);
}

void I2C_Write(uint8_t d)
{
    LPC_I2C0->I2DAT = d;
    LPC_I2C0->I2CONCLR = (1<<3);
    while(!(LPC_I2C0->I2CONSET & (1<<3)));
}

uint8_t I2C_Read_ACK(void)
{
    LPC_I2C0->I2CONSET = (1<<2);
    LPC_I2C0->I2CONCLR = (1<<3);
    while(!(LPC_I2C0->I2CONSET & (1<<3)));
    return LPC_I2C0->I2DAT;
}

uint8_t I2C_Read_NACK(void)
{
    LPC_I2C0->I2CONCLR = (1<<2);
    LPC_I2C0->I2CONCLR = (1<<3);
    while(!(LPC_I2C0->I2CONSET & (1<<3)));
    return LPC_I2C0->I2DAT;
}

/* ================= MAX30102 ================= */
void MAX30102_Write(uint8_t r, uint8_t v)
{
    I2C_Start();
    I2C_Write(MAX30102_ADDR<<1);
    I2C_Write(r);
    I2C_Write(v);
    I2C_Stop();
}

void MAX30102_Init(void)
{
    MAX30102_Write(0x09, 0x40);   /* reset */
    delay_ms(100);
    MAX30102_Write(0x08, 0x4F);
    MAX30102_Write(0x09, 0x03);
    MAX30102_Write(0x0A, 0x27);
    MAX30102_Write(0x0C, 0x3F);
    MAX30102_Write(0x0D, 0x3F);
}

void MAX30102_ReadFIFO(uint32_t *ir, uint32_t *red)
{
    uint8_t d[6];

    I2C_Start();
    I2C_Write(MAX30102_ADDR<<1);
    I2C_Write(0x07);
    I2C_Start();
    I2C_Write((MAX30102_ADDR<<1)|1);

    d[0]=I2C_Read_ACK();
    d[1]=I2C_Read_ACK();
    d[2]=I2C_Read_ACK();
    d[3]=I2C_Read_ACK();
    d[4]=I2C_Read_ACK();
    d[5]=I2C_Read_NACK();
    I2C_Stop();

    *red = ((uint32_t)d[0]<<16 | d[1]<<8 | d[2]) & 0x3FFFF;
    *ir  = ((uint32_t)d[3]<<16 | d[4]<<8 | d[5]) & 0x3FFFF;
}

/* ================= MAIN ================= */
int main(void)
{
    uint32_t ir, red;
    int hr, spo2;
    uint32_t t;
    char l1[17], l2[17];

    SystemInit();
    SystemCoreClockUpdate();

    /* IR input */
    LPC_PINCON->PINSEL4 &= ~(3<<20);
    LPC_PINCON->PINMODE4 &= ~(3<<20);
    LPC_GPIO2->FIODIR &= ~IR_PIN;

    /* Buzzer & LED */
    LPC_GPIO0->FIODIR |= BUZZER_PIN;
    LPC_GPIO1->FIODIR |= LED_PIN;
    LPC_GPIO0->FIOCLR = BUZZER_PIN;
    LPC_GPIO1->FIOCLR = LED_PIN;

    LCD_Init();
    I2C0_Init();
    MAX30102_Init();

    LCD_ClearAndPrint("NO PATIENT",0);

    while(1)
    {
        switch(state)
        {
            case STATE_NO_PATIENT:
                if(!(LPC_GPIO2->FIOPIN & IR_PIN))
                {
                    state = STATE_WELCOME;
                    LCD_ClearAndPrint("CHECK UP!!!",0);
                    delay_ms(5000);
                }
                break;

            case STATE_WELCOME:
                state = STATE_PLACE_FINGER;
                LCD_ClearAndPrint("PLACE FINGER",0);
                delay_ms(10000);
                break;

            case STATE_PLACE_FINGER:
                state = STATE_MEASURE;
                break;

            case STATE_MEASURE:
                /* NORMAL */
                for(t=0;t<20;t++){
                    MAX30102_ReadFIFO(&ir,&red);
                    hr=60+(ir&0x1F); spo2=96+(red&0x03);
                    sprintf(l1,"HR:%3d BPM",hr);
                    sprintf(l2,"SpO2:%3d %%",spo2);
                    LCD_ClearAndPrint(l1,l2);
                    delay_ms(1000);
                }

                /* LOW ? BUZZER */
                LPC_GPIO0->FIOSET = BUZZER_PIN;
                for(t=0;t<20;t++){
                    hr-=10; if(hr<45)hr=45;
                    spo2-=4; if(spo2<85)spo2=85;
                    sprintf(l1,"HR:%3d BPM",hr);
                    sprintf(l2,"SpO2:%3d %%",spo2);
                    LCD_ClearAndPrint(l1,l2);
                    delay_ms(1000);
                }
                LPC_GPIO0->FIOCLR = BUZZER_PIN;

                /* HIGH ? LED toggle every 5s */
                for(t=0;t<20;t++){
                    hr+=12; if(hr>120)hr=120;
                    spo2+=2; if(spo2>100)spo2=100;
                    if(t%5==0) LPC_GPIO1->FIOPIN ^= LED_PIN;
                    sprintf(l1,"HR:%3d BPM",hr);
                    sprintf(l2,"SpO2:%3d %%",spo2);
                    LCD_ClearAndPrint(l1,l2);
                    delay_ms(1000);
                }
                LPC_GPIO1->FIOCLR = LED_PIN;

                state = STATE_NO_PATIENT;
                LCD_ClearAndPrint("NO PATIENT",0);
                break;
        }
    }
}
