#include <iostream>
#include <string>
#include <vector>

using namespace std;

class Order {
    private:
        string customer_name;
        vector<string> items;
        bool placed;
        float total_amount;

    public:
        Order(string name) {
            this->customer_name = name;
            this->placed = false;
            this->total_amount = 0;
            items.clear();
        }

        void add_item(string item, int price) {
            if (placed) {
                cout << "Order already placed can't add more items." << endl;
                return;
            }
            items.push_back(item);
            total_amount += price;
            cout << "Item added: " << item << endl;
        }

        void get_order_summary() {
            cout << "Customer name: " << customer_name << endl;
            cout << "Items: " << endl;
            for (string item : items) {
                cout << "- " << item << endl;
            }
            cout << "Total amount: " << total_amount << endl;
            cout << "Order placed: " << (placed ? "Yes" : "No") << endl;
        }

        void place_order() {
            if (placed) {
                cout << "Order already placed." << endl;
                return;
            }
            if (items.size() == 0) {
                cout << "Cart is empty can't place order." << endl;
                return;
            }
            placed = true;
            cout << "Order placed successfully." << endl;
        }

        ~Order() {
            cout << "Order cancelled and resources released." << endl;
        }
};

int main() {
    Order order("Alice");

    order.add_item("Burger", 150);
    order.add_item("Fries", 80);

    order.get_order_summary();

    order.place_order();

    return 0;
}
