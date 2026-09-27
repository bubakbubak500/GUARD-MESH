#pragma once
#include <cstddef>
#include <cstdint>

typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef uint32_t StackType_t;
struct StaticTask_t { int unused; };
struct portMUX_TYPE { bool locked; };
#define portMUX_INITIALIZER_UNLOCKED {false}
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)

namespace fake { void enter(portMUX_TYPE *); void leave(portMUX_TYPE *); }
#define portENTER_CRITICAL(mux) fake::enter(mux)
#define portEXIT_CRITICAL(mux) fake::leave(mux)
