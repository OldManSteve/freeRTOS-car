
#include "line_sensor_init.h"
#include "ultrasonic_sensor.h"
#include "car_config.h"
#include "motor_init.h"

/*!
 * @brief Check if the car is on the line
 * @param params
 * @return
 */
uint8_t
check_line_touch(void *params)
{
    car_struct_t *car_struct = (car_struct_t *)params;

    return (car_struct->obs->left_sensor_detected << 1)
           | (car_struct->obs->right_sensor_detected);
}

void
motor_control_task(void *params)
{
    car_struct_t *car_struct = (car_struct_t *)params;

    set_wheel_direction(DIRECTION_FORWARD);
    set_wheel_speed_synced(90u, car_struct);

    for (;;)
    {
        uint8_t temp = check_line_touch(car_struct);
        switch (temp)
        {
            default:
                break;
            case 0b01: // right
                car_struct->p_right_motor->speed.distance_cm -=
                    SLOT_DISTANCE_CM;
                break;
            case 0b10: //left
                car_struct->p_right_motor->speed.distance_cm +=
                    SLOT_DISTANCE_CM;
                break;
            case 0b11:
                revert_wheel_direction();
                distance_to_stop(car_struct, 17.f);
                turn(DIRECTION_LEFT, 90u, 90u, car_struct);
                set_wheel_direction(DIRECTION_FORWARD);
                set_wheel_speed_synced(90u, car_struct);
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void
h_main_irq_handler(void)
{
    if (gpio_get_irq_event_mask(SPEED_PIN_LEFT) & GPIO_IRQ_EDGE_FALL)
    {
        gpio_acknowledge_irq(SPEED_PIN_LEFT, GPIO_IRQ_EDGE_FALL);

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(g_left_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    if (gpio_get_irq_event_mask(SPEED_PIN_RIGHT) & GPIO_IRQ_EDGE_FALL)
    {
        gpio_acknowledge_irq(SPEED_PIN_RIGHT, GPIO_IRQ_EDGE_FALL);

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(g_right_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

int
main(void)
{
    stdio_usb_init();

    obs_t obs;

    motor_t     motor_right;
    motor_t     motor_left;
    motor_pid_t pid;

    direction_t direction;

    car_struct_t car_struct = { .p_right_motor = &motor_right,
                                .p_left_motor  = &motor_left,
                                .p_pid         = &pid,
                                .obs           = &obs,
                                .p_direction   = &direction };

    // line
    line_sensor_init(&car_struct);
    line_tasks_init(&car_struct);
    printf("Line sensor initialized!\n");

    // motor
    motor_init(&car_struct);
    motor_tasks_init(&car_struct, &h_main_irq_handler);
    printf("Motor initialized!\n");

    sleep_ms(1000u);

    // control task
    TaskHandle_t h_motor_turning_task_handle = NULL;
    xTaskCreate(motor_control_task,
                "motor_turning_task",
                configMINIMAL_STACK_SIZE,
                (void *)&car_struct,
                PRIO,
                &h_motor_turning_task_handle);

    // PID timer
    struct repeating_timer pid_timer;
    add_repeating_timer_ms(-50, repeating_pid_handler, &car_struct, &pid_timer);

    vTaskStartScheduler();

    return (0);
}
