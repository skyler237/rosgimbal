#include "mpu6000.h"

uint8_t raw[15] = {MPU_RA_ACCEL_XOUT_H | 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

MPU6000* IMU_Ptr;
void data_transfer_cb(void)
{
  IMU_Ptr->data_transfer_callback();
}

void MPU6000::write(uint8_t reg, uint8_t data)
{
  spi->enable();
  spi->transfer_byte(reg);
  spi->transfer_byte(data);
  spi->disable();
  delayMicroseconds(1);
}

MPU6000::MPU6000(SPI* spi_drv) {
  IMU_Ptr = this;
  spi = spi_drv;

  write(MPU_RA_PWR_MGMT_1, MPU_BIT_H_RESET);
  delay(150);

  write(MPU_RA_PWR_MGMT_1, MPU_CLK_SEL_PLLGYROZ);
  write(MPU_RA_USER_CTRL, MPU_BIT_I2C_IF_DIS);
  write(MPU_RA_PWR_MGMT_2, 0x00);
  write(MPU_RA_SMPLRT_DIV, 0x00);
  write(MPU_RA_CONFIG, MPU_BITS_DLPF_CFG_98HZ);
  write(MPU_RA_ACCEL_CONFIG, MPU_BITS_FS_4G);
  write(MPU_RA_GYRO_CONFIG, MPU_BITS_FS_2000DPS);

  spi->set_divisor(2); // 21 MHz SPI clock (within 20 +/- 10%)

  // set the accel and gyro scale parameters
  accel_scale_ = (4.0 * 9.80665f) / ((float)0x7FFF);
  gyro_scale_= (2000.0 * 3.14159f/180.0f) / ((float)0x7FFF);

  spi->register_complete_cb(&data_transfer_cb);
}

void MPU6000::update()
{
  static auto last_update_time_us = 0;
  if (micros() > last_update_time_us + 1000)
  {
    raw[0] = MPU_RA_ACCEL_XOUT_H | 0x80;
    spi->transfer(raw, 15, raw);
  }
}

void MPU6000::data_transfer_callback()
{
  new_data_ = true;
  acc_[0] = (float)((int16_t)((raw[1] << 8) | raw[2])) * accel_scale_;
  acc_[1] = (float)((int16_t)((raw[3] << 8) | raw[4])) * accel_scale_;
  acc_[2] = (float)((int16_t)((raw[5] << 8) | raw[6])) * accel_scale_;

  temp_  = (float)((int16_t)((raw[7] << 8) | raw[8])) / 340.0f * 36.53f;

  gyro_[0]  = (float)((int16_t)((raw[9]  << 8) | raw[10])) * gyro_scale_;
  gyro_[1]  = (float)((int16_t)((raw[11] << 8) | raw[12])) * gyro_scale_;
  gyro_[2]  = (float)((int16_t)((raw[13] << 8) | raw[14])) * gyro_scale_;
}

void MPU6000::read(float (&accel_data)[3], float (&gyro_data)[3], float* temp_data)
{
  accel_data[0] = acc_[0];
  accel_data[1] = acc_[1];
  accel_data[2] = acc_[2];
  gyro_data[0] = gyro_[0];
  gyro_data[1] = gyro_[1];
  gyro_data[2] = gyro_[2];
  *temp_data = temp_;
}
