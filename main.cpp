#include <iostream>
#include <memory>
#include "Orderbook.h"
#include "Order.h"
#include "Types.h"
#include "OrderType.h"

int main() {
    Orderbook orderbook;
    const OrderId orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
    std::cout << orderbook.Size() << '\n'; // 1
    orderbook.CancelOrder(orderId);
    std::cout << orderbook.Size() << '\n'; // 0





    return 0;
}