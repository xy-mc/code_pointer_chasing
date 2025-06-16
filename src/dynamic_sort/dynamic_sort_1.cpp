#include <list>
#include <random>
#include <iostream>
#include "/home/luoqiang/xymc/gem5_dda/include/gem5/m5ops.h"

using namespace std;

const int record_number = 32;
struct Record {
    int value[record_number];
};

// 比较函数，用于按指定位置比较两个Record
class CompareByPosition {
private:
    size_t pos;
public:
    CompareByPosition(size_t position) : pos(position) {}
    bool operator()(const Record& a, const Record& b) const {
        return a.value[pos] < b.value[pos];
    }
};

// 随机生成一个包含指定数量元素的列表，元素值在 [min, max] 范围内
list<Record> generate_random_list(size_t size, int min, int max) {
    list<Record> lst;
    random_device rd;  // 随机数种子
    mt19937 gen(rd()); // 随机数引擎
    uniform_int_distribution<int> dis(min, max); // 均匀分布

    for (size_t i = 0; i < size; ++i) {
        Record record;
        for (size_t j = 0; j < record_number; j++) {
            record.value[j] = dis(gen);
        }
        lst.push_back(record);
    }
    return lst;
}

// 随机删除 y 个元素
void random_remove(list<Record>& lst, size_t remove_count, int min_value, int max_value) {
    if (lst.empty()) return;
    
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> pos_dis(0, lst.size() - 1);

    for (size_t i = 0; i < remove_count && !lst.empty(); ++i) {
        size_t pos = pos_dis(gen);  // 随机选择一个位置
        auto it = lst.begin();
        advance(it, pos);  // 移动到随机位置
        lst.erase(it);     // 删除该位置的元素
    }
}

// 随机插入 z 个元素
void random_insert(list<Record>& lst, size_t insert_count, int min_value, int max_value) {
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> val_dis(min_value, max_value);
    uniform_int_distribution<> pos_dis(0, lst.size());

    for (size_t i = 0; i < insert_count; ++i) {
        Record new_record;
        // 填充新记录的值数组
        for (size_t j = 0; j < record_number; j++) {
            new_record.value[j] = val_dis(gen);
        }
        
        // 随机选择插入位置
        size_t pos = pos_dis(gen);
        auto it = lst.begin();
        advance(it, pos);
        
        lst.insert(it, new_record);
    }
}

// 主测试函数
void test_list_operations(list<Record>& lst, size_t initial_size, int min_value, int max_value,
                            size_t remove_count, size_t insert_count, size_t iterations) {

    for (size_t i = 0; i < iterations; ++i) {
        random_remove(lst, remove_count, min_value, max_value);
        random_insert(lst, insert_count, min_value, max_value);
    }
}

// void print_list(const list<Record>& lst, size_t pos) {
//     for (const auto& record : lst) {
//         cout << record.value[pos] << " ";
//     }
//     cout << endl;
// }
 
int main() {
    // 参数设置
    size_t initial_size = 10000; // 初始链表大小
    int min_value = 1;           // 随机数最小值
    int max_value = 10000;         // 随机数最大值
    size_t remove_count = 100;     // 每次删除的元素数量
    size_t insert_count = 100;     // 每次插入的元素数量
    size_t iterations = 50;      // 重复删除和插入的次数

    list<Record> lst = generate_random_list(initial_size, min_value, max_value);
    test_list_operations(lst, initial_size, min_value, max_value,
                            remove_count, insert_count, iterations);
    // m5_checkpoint(0, 0);
    // m5_reset_stats(0, 0);
    
    // 对每个位置进行排序
    for (size_t pos = 0; pos < record_number; pos++) {
        lst.sort(CompareByPosition(pos));
        // cout << "After sorting by position " << pos << ":" << endl;
        // print_list(lst, pos);
    }
    
    // m5_dump_stats(0, 0);

    return 0;
}

