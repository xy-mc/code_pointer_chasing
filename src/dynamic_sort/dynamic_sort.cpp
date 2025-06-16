#include <list>
#include <random>
#include <iostream>
#include "/home/luoqiang/xymc/gem5_dda/include/gem5/m5ops.h"

using namespace std;

const int record_number = 64;
#define stride 16

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
void random_remove(list<Record>& lst, int min_value, int max_value) {
    if (lst.empty()) return;

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> val_dis(min_value, max_value);

    int target = val_dis(gen);

    for (auto it = lst.begin(); it != lst.end(); ) {
        if (it->value[0] == target) {
            it = lst.erase(it); // erase 返回下一个有效迭代器
        } else {
            ++it;
        }
    }
}

// 随机插入 z 个元素
void random_insert(list<Record>& lst, size_t insert_count, int min_value, int max_value) {
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> val_dis(min_value, max_value);

    for (size_t i = 0; i < insert_count; ++i) {
        Record new_record;
        for (size_t j = 0; j < record_number; j++) {
            new_record.value[j] = val_dis(gen);
        }
        lst.push_back(new_record); // 直接插入到末尾
    }
}

// 主测试函数
void test_list_operations(list<Record>& lst, size_t initial_size, int min_value, int max_value,
                            size_t remove_count, size_t insert_count, size_t iterations) {

    for (size_t i = 0; i < iterations; ++i) {
        random_insert(lst, insert_count, min_value, max_value);
        random_remove(lst, min_value, max_value);
    }
}

// void print_list(const list<Record>& lst, size_t pos) {
//     for (const auto& record : lst) {
//         cout << record.value[pos] << " ";
//     }
//     cout << endl;
// }

void print_list_addresses(const list<Record>& lst) {
    for (auto it = lst.begin(); it != lst.end(); ++it) {
        printf("%p\n", (const void*)&(*it));
    }
}

void print_list_offsets(const list<Record>& lst) {
    if (lst.size() < 2) return;
    auto prev = lst.begin();
    auto it = prev;
    ++it;
    for (; it != lst.end(); ++it, ++prev) {
        printf("offset = %ld\n", (char*)&(*it) - (char*)&(*prev));
    }
}

int main() {
    // 参数设置
    size_t initial_size = 10000; // 初始链表大小
    int min_value = 1;           // 随机数最小值
    int max_value = 30;         // 随机数最大值
    size_t remove_count = 100;     // 每次删除的元素数量
    size_t insert_count = 50;     // 每次插入的元素数量
    size_t iterations = 100;      // 重复删除和插入的次数

    list<Record> lst = generate_random_list(initial_size, min_value, max_value);
    
    test_list_operations(lst, initial_size, min_value, max_value,
                            remove_count, insert_count, iterations);

    m5_checkpoint(0, 0);
    m5_reset_stats(0, 0); 
    
    // printf("lst.size() = %d\n", lst.size());
    // print_list_addresses(lst);
    // print_list_offsets(lst);
    // 对每个位置进行排序
    for (size_t pos = 0; pos < record_number; pos += stride) {
        lst.sort(CompareByPosition(pos));
    }
    
    m5_dump_stats(0, 0);

    return 0;
}

