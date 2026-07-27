# risksim_lob — header-only limit-order-book matching engine.
add_library(risksim_lob INTERFACE)
target_include_directories(risksim_lob INTERFACE include)
add_library(risksim::lob ALIAS risksim_lob)
