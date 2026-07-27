# risksim_market — fire-sale (price-mediated) contagion over the order book.
add_library(risksim_market STATIC src/fire_sale.cpp)
target_include_directories(risksim_market PUBLIC include)
target_link_libraries(risksim_market
    PUBLIC risksim::core risksim::lob
    PRIVATE risksim_options)

add_library(risksim::market ALIAS risksim_market)
