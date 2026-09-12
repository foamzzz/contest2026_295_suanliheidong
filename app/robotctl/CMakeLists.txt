if(CONFIG_LVX_USE_DEMO_CONTEST2026_295_ROBOTCTL)
  nuttx_add_application(
    NAME robotctl
    SRCS robotctl_main.c robotctl_servo.c
    STACKSIZE 4096)
endif()
