set(EVM_SRCS interpreter.cpp opcode_handlers.cpp gas_storage_cost.cpp
             evm_cache.cpp opcode_gas_trace.cpp
)

add_library(evm OBJECT ${EVM_SRCS})
