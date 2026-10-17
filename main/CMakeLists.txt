# main/CMakeLists.txt —— 「Token 观测站」应用。
# demo_*.c 参考页保留在仓库中但不参与本应用编译(派生应用不复用测试外壳)。
idf_component_register(
    SRCS "main.c"
         "jpath.c"
         "provider.c"
         "app_sched.c"
         "wifi_sta.c"
         "net_fetch.c"
         "app_core.c"
         "udp_log.c"
         "setup_portal.c"
         "app_cfg.c"
         "ui_dashboard.c"
         "../assets/fonts/app_font_14.c"
         "../assets/fonts/app_font_24.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_netif esp_timer esp_wifi nvs_flash esp-tls lwip mbedtls esp_http_server
)
