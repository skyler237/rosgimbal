#include "i2c.h"

#define while_check(cond, timeout) \
  {\
    int32_t timeout_var = timeout; \
    while ((cond) && timeout_var) \
      timeout_var--; \
    if (!timeout_var) \
    { \
      handle_hardware_failure();\
      return false; \
    }\
  }

//global i2c ptrs used by the event interrupts
I2C* I2C1_Ptr;
I2C* I2C2_Ptr;

void I2C::init(I2C_TypeDef *I2C)
{
  dev_ = I2C;
  //enable peripheral clocks as we need them
  if (dev_ == I2C1)
  {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

    //configure the gpio pins
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource8, GPIO_AF_I2C1);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF_I2C1);
    sda_.init(GPIOB, GPIO_Pin_9, GPIO::PERIPH_IN_OUT);
    scl_.init(GPIOB, GPIO_Pin_8, GPIO::PERIPH_IN_OUT);
    I2C1_Ptr = this;
    DMA_stream_ = DMA1_Stream0;
    DMA_channel_ = DMA_Channel_1;
    DMA_Stream_TCFLAG_ = DMA_FLAG_TCIF0;
    DMA_IRQn_ = DMA1_Stream0_IRQn;
    I2C_EV_IRQn_ = I2C1_EV_IRQn;
    I2C_ER_IRQn_ = I2C1_ER_IRQn;
  }
  else if (dev_ == I2C2)
  {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

    //configure the gpio pins
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_I2C2);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_I2C2);
    sda_.init(GPIOB, GPIO_Pin_11, GPIO::PERIPH_IN_OUT);
    scl_.init(GPIOB, GPIO_Pin_10, GPIO::PERIPH_IN_OUT);
    I2C2_Ptr = this;
    DMA_stream_ = DMA1_Stream2;
    DMA_channel_ = DMA_Channel_7;
    DMA_Stream_TCFLAG_ = DMA_FLAG_TCIF2;
    DMA_IRQn_ = DMA1_Stream2_IRQn;
    I2C_EV_IRQn_ = I2C2_EV_IRQn;
    I2C_ER_IRQn_ = I2C2_ER_IRQn;
  }

  unstick(); //unstick will properly initialize pins

  //initialze the i2c itself
  I2C_DeInit(dev_);

  I2C_InitTypeDef I2C_InitStructure;
  I2C_StructInit(&I2C_InitStructure);
  I2C_InitStructure.I2C_ClockSpeed = 400000;
  I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
  I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
  I2C_InitStructure.I2C_OwnAddress1 = 0; //The first device address
  I2C_InitStructure.I2C_Ack = I2C_Ack_Disable;
  I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
  I2C_Init(dev_, &I2C_InitStructure);

  // Interrupts
  NVIC_InitTypeDef NVIC_InitStructure;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x02;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x01;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_InitStructure.NVIC_IRQChannel = I2C_EV_IRQn_;
  NVIC_Init(&NVIC_InitStructure);

  // I2C Event Interrupt
  NVIC_InitStructure.NVIC_IRQChannel = I2C_ER_IRQn_;
  NVIC_Init(&NVIC_InitStructure);

  // DMA Event Interrupt
  NVIC_InitStructure.NVIC_IRQChannel = DMA_IRQn_;
  NVIC_Init(&NVIC_InitStructure);

  DMA_Cmd(DMA_stream_, DISABLE);
  DMA_DeInit(DMA_stream_);
  DMA_InitStructure_.DMA_FIFOMode = DMA_FIFOMode_Enable;
  DMA_InitStructure_.DMA_FIFOThreshold = DMA_FIFOThreshold_Full ;
  DMA_InitStructure_.DMA_MemoryBurst = DMA_MemoryBurst_Single ;
  DMA_InitStructure_.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
  DMA_InitStructure_.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
  DMA_InitStructure_.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  DMA_InitStructure_.DMA_MemoryInc = DMA_MemoryInc_Enable;
  DMA_InitStructure_.DMA_Mode = DMA_Mode_Normal;
  DMA_InitStructure_.DMA_Channel = DMA_channel_;

  DMA_InitStructure_.DMA_PeripheralBaseAddr = (uint32_t)(&(dev_->DR));
  DMA_InitStructure_.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
  DMA_InitStructure_.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
  DMA_InitStructure_.DMA_Priority = DMA_Priority_High;
  DMA_InitStructure_.DMA_DIR = DMA_DIR_PeripheralToMemory;

  I2C_Cmd(dev_, ENABLE);

  buffer_head_ = 0;
  buffer_tail_ = 0;
}

void I2C::unstick()
{
  scl_.set_mode(GPIO::OUTPUT);
  sda_.set_mode(GPIO::OUTPUT);

  scl_.write(GPIO::HIGH);
  sda_.write(GPIO::HIGH);

  for (int i = 0; i < 8; ++i)
  {
    delayMicroseconds(1);
    scl_.toggle();
  }

  sda_.write(GPIO::LOW);
  delayMicroseconds(1);
  scl_.write(GPIO::LOW);
  delayMicroseconds(1);

  scl_.write(GPIO::HIGH);
  delayMicroseconds(1);
  sda_.write(GPIO::HIGH);
  delayMicroseconds(1);

  scl_.set_mode(GPIO::PERIPH_IN_OUT);
  sda_.set_mode(GPIO::PERIPH_IN_OUT);
  current_status_ = IDLE;
}


int8_t I2C::read(uint8_t addr, uint8_t reg, uint8_t num_bytes, uint8_t* data, std::function<void(void)> callback, bool blocking)
{
  // load job into the buffer
  i2c_job_t* job = &job_buffer_[buffer_tail_];
  buffer_tail_ = (buffer_tail_ + 1) % I2C_JOB_BUFFER_SIZE;
  job->type = READ;
  job->reg = reg;
  job->addr = addr;
  job->num_bytes = num_bytes;
  job->data = data;
  job->callback = callback;

  // if this was a new job, fire it off
  if (current_status_ == IDLE)
  {
    handle_job();
  }

  if (blocking)
  {
    while(current_status_ != IDLE);
  }

  return SUCCESS;
}

// asynchronous write, for commanding adc conversions
int8_t I2C::write(uint8_t addr, uint8_t reg, uint8_t* data, std::function<void(void)> callback)
{
  // load job into the buffer
  i2c_job_t* job = &job_buffer_[buffer_tail_];
  buffer_tail_ = (buffer_tail_ + 1) % I2C_JOB_BUFFER_SIZE;
  job->type = WRITE;
  job->reg = reg;
  job->addr = addr;
  job->num_bytes = 1;
  job->data = data;
  job->callback = callback;

  // if this was a new job, fire it off
  if (current_status_ == IDLE)
  {
    handle_job();
  }

  return SUCCESS;
}

bool I2C::handle_job()
{
  i2c_job_t* job = &job_buffer_[buffer_head_];
  buffer_head_ = (buffer_head_ + 1) % I2C_JOB_BUFFER_SIZE;

  addr_ = job->addr << 1;
  cb_ = job->callback;
  reg_ = job->reg;
  subaddress_sent_ = (reg_ == 0xFF);
  len_ = job->num_bytes;
  done_ = false;

  if (job->type == WRITE)
  {
    current_status_ = WRITING;
  }
  else
  {
    current_status_ = READING;

    DMA_DeInit(DMA_stream_);
    DMA_InitStructure_.DMA_BufferSize = (uint16_t)(len_);
    DMA_InitStructure_.DMA_Memory0BaseAddr = (uint32_t) job->data;
    DMA_Init(DMA_stream_, &DMA_InitStructure_);
  }

  I2C_Cmd(dev_, ENABLE);

  while_check (I2C_GetFlagStatus(dev_, I2C_FLAG_BUSY), 5000);

  I2C_GenerateSTART(dev_, ENABLE);

  I2C_ITConfig(dev_, I2C_IT_EVT | I2C_IT_ERR, ENABLE);

  return true;
}


void I2C::transfer_complete_cb()
{
  if (buffer_head_ == buffer_tail_)
    current_status_ = IDLE;
  else
    handle_job();

  if (cb_)
    cb_();
}


// blocking, single register read (for configuring devices)
int8_t I2C::read(uint8_t addr, uint8_t reg, uint8_t *data)
{
  while_check (current_status_ != IDLE, 50000)

  int8_t return_code = ERROR;

  while_check (I2C_GetFlagStatus(dev_, I2C_FLAG_BUSY), 5000);

  I2C_Cmd(dev_, ENABLE);
  if (reg != 0xFF)
  {
    I2C_GenerateSTART(dev_, ENABLE);
    while_check (!I2C_CheckEvent(dev_, I2C_EVENT_MASTER_MODE_SELECT), 5000);
    I2C_Send7bitAddress(dev_, addr << 1, I2C_Direction_Transmitter);
    uint32_t timeout = 500;
    while (!I2C_CheckEvent(dev_, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) && --timeout);
    if (!timeout)
    {
      I2C_GenerateSTOP(dev_, ENABLE);
      I2C_Cmd(dev_, DISABLE);
      return ERROR;
    }
    I2C_Cmd(dev_, ENABLE);
    I2C_SendData(dev_, reg);
    while_check (!I2C_CheckEvent(dev_, I2C_EVENT_MASTER_BYTE_TRANSMITTED), 5000);
  }

  // Read the byte
  I2C_AcknowledgeConfig(dev_, DISABLE);
  I2C_GenerateSTART(dev_, ENABLE);
  while_check (!I2C_CheckEvent(dev_, I2C_EVENT_MASTER_MODE_SELECT), 5000);
  I2C_Cmd(dev_, ENABLE);
  I2C_Send7bitAddress(dev_, addr << 1, I2C_Direction_Receiver);
  uint32_t timeout = 500;
  while (!I2C_CheckEvent(dev_, I2C_EVENT_MASTER_BYTE_RECEIVED) && --timeout);
  if (timeout)
  {
    return_code = SUCCESS;
    *data = I2C_ReceiveData(dev_);
  }
  I2C_GenerateSTOP(dev_, ENABLE);
  I2C_Cmd(dev_, DISABLE);

  return return_code;
}


// blocking, single register write (for configuring devices)
int8_t I2C::write(uint8_t addr, uint8_t reg, uint8_t data)
{
  while_check (current_status_ != IDLE, 50000);
  while_check (I2C_GetFlagStatus(dev_, I2C_FLAG_BUSY), 5000);
  I2C_Cmd(dev_, ENABLE);

  // start the transfer
  I2C_GenerateSTART(dev_, ENABLE);
  while_check (!I2C_CheckEvent(dev_, I2C_EVENT_MASTER_MODE_SELECT), 5000);
  I2C_Send7bitAddress(dev_, addr << 1, I2C_Direction_Transmitter);
  uint32_t timeout = 500;
  while (!I2C_CheckEvent(dev_, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) && --timeout);
  if (!timeout)
  {
    I2C_GenerateSTOP(dev_, ENABLE);
    I2C_Cmd(dev_, DISABLE);
    return ERROR;
  }
  I2C_Cmd(dev_, ENABLE);

  // Send the register
  if (reg != 0xFF)
  {
    I2C_SendData(dev_, reg);
    while_check (!I2C_CheckEvent(dev_, I2C_EVENT_MASTER_BYTE_TRANSMITTED), 5000);
  }

  // Write the byte with a NACK
  I2C_AcknowledgeConfig(dev_, DISABLE);
  I2C_SendData(dev_, data);
  while_check (!I2C_CheckEvent(dev_, I2C_EVENT_MASTER_BYTE_TRANSMITTED), 5000);
  I2C_GenerateSTOP(dev_, ENABLE  );
  I2C_Cmd(dev_, DISABLE);
  return SUCCESS;

}

// if for some reason, a step in an I2C read or write fails, call this
void I2C::handle_hardware_failure() {
  error_count_++;
  unstick(); //unstick and reinitialize the hardware
}


// This is the I2C_IT_ERR handler
bool I2C::handle_error()
{
  I2C_Cmd(dev_, DISABLE);
  while_check (I2C_GetFlagStatus(dev_, I2C_FLAG_BUSY), 5000);

  // Turn off the interrupts
  I2C_ITConfig(dev_, I2C_IT_EVT | I2C_IT_ERR, DISABLE);

  //reset errors
  I2C_ClearFlag(dev_, I2C_SR1_OVR | I2C_SR1_AF | I2C_SR1_ARLO | I2C_SR1_BERR);
  current_status_ = IDLE;
}

// This is the I2C_IT_EV handler
bool I2C::handle_event()
{
  uint32_t last_event = I2C_GetLastEvent(dev_);

  // We just sent a byte
  if (last_event == I2C_EVENT_MASTER_BYTE_TRANSMITTED)
  {
    // If we are reading, then we just sent a subaddress and need to send
    // a repeated start, and enable the DMA NACK
    if (current_status_ == READING)
    {
      I2C_AcknowledgeConfig(dev_, ENABLE);
      I2C_DMALastTransferCmd(dev_, ENABLE);
      I2C_GenerateSTART(dev_, ENABLE);
    }
    // We are in write mode and are done, need to clean up
    else
    {
      I2C_GenerateSTOP(dev_, ENABLE);
      I2C_ITConfig(dev_, I2C_IT_EVT, DISABLE);
      transfer_complete_cb();
    }
  }

  // We just sent the address in write mode
  if (last_event == I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)
  {
    // We need to send the subaddress
    if (!subaddress_sent_)
    {
      I2C_SendData(dev_, reg_);
      subaddress_sent_ = true;
      if (current_status_ == WRITING)
      {
        I2C_SendData(dev_, data_);
        done_ = true;
      }
    }
    // We need to send our data (no subaddress)
    else
    {
      I2C_SendData(dev_, data_);
      done_ = true;
    }
  }

  // We are in receiving mode, preparing to receive the big DMA dump
  if (last_event == I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)
  {
    I2C_ITConfig(dev_, I2C_IT_EVT, DISABLE);
    DMA_SetCurrDataCounter(DMA_stream_, len_);
    I2C_DMACmd(dev_, ENABLE);
    DMA_ITConfig(DMA_stream_, DMA_IT_TC, ENABLE);
    DMA_Cmd(DMA_stream_, ENABLE);
  }

  // Start just sent
  if (last_event == I2C_EVENT_MASTER_MODE_SELECT)
  {
    // we either don't need to send, or already sent the subaddress
    if (subaddress_sent_ && current_status_ == READING)
    {
      // Set up a receive
      I2C_Send7bitAddress(dev_, addr_, I2C_Direction_Receiver);
    }
    // We need to either send the subaddress or our datas
    else
    {
      // Set up a write
      I2C_Send7bitAddress(dev_, addr_, I2C_Direction_Transmitter);
    }
  }
}

extern "C"
{

// C-based IRQ functions (defined in the STD lib somewhere)
void DMA1_Stream2_IRQHandler(void)
{

  if (DMA_GetFlagStatus(DMA1_Stream2, DMA_FLAG_TCIF2))
  {
    /* Clear transmission complete flag */
    DMA_ClearFlag(DMA1_Stream2, DMA_FLAG_TCIF2);

    I2C_DMACmd(I2C2, DISABLE);
    /* Send I2C1 STOP Condition */
    I2C_GenerateSTOP(I2C2, ENABLE);
    /* Disable DMA channel*/
    DMA_Cmd(DMA1_Stream2, DISABLE);

    I2C2_Ptr->transfer_complete_cb();
  }
}

void DMA1_Stream0_IRQHandler(void)
{
  if (DMA_GetFlagStatus(DMA1_Stream0, DMA_FLAG_TCIF0))
  {
    /* Clear transmission complete flag */
    DMA_ClearFlag(DMA1_Stream0, DMA_FLAG_TCIF0);

    I2C_DMACmd(I2C1, DISABLE);
    /* Send I2C1 STOP Condition */
    I2C_GenerateSTOP(I2C1, ENABLE);
    /* Disable DMA channel*/
    DMA_Cmd(DMA1_Stream0, DISABLE);

    I2C1_Ptr->transfer_complete_cb();
  }
}

void I2C1_ER_IRQHandler(void) {
  I2C1_Ptr->handle_error();
}

void I2C1_EV_IRQHandler(void) {
  I2C1_Ptr->handle_event();
}

void I2C2_ER_IRQHandler(void) {
  I2C2_Ptr->handle_error();
}

void I2C2_EV_IRQHandler(void) {
  I2C2_Ptr->handle_event();
}

}
