/*
#include <list>
#include <random>
#include <iostream>
#include "/home/luoqiang/xymc/gem5_dda/include/gem5/m5ops.h"

using namespace std;

// 生成一个已排序的随机列表
list<int> generate_sorted_list(size_t size, int min_val, int max_val) {
    list<int> lst;
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(min_val, max_val);

    for (size_t i = 0; i < size; ++i) {
        lst.push_back(dis(gen));
    }
    lst.sort();  // 保证列表有序
    return lst;
}

// 测试合并操作的主函数
void test_merge_operations(list<int>& main_list, size_t initial_size, int min_val, int max_val,
                          size_t merge_size, size_t iterations) {

    for (size_t i = 0; i < iterations; ++i) {
        // 每次生成一个新的待合并列表（已排序）
        list<int> to_merge = generate_sorted_list(merge_size, min_val, max_val);

        // 执行合并操作
        main_list.merge(to_merge);

        // 合并后 to_merge 为空，无需后续处理
    }
}

int main() {
    size_t initial_size = 10000;  // 主列表初始大小
    int min_val = 1;              // 随机数最小值
    int max_val = 10000000;            // 随机数最大值
    size_t merge_size = 1000;     // 每次合并的子列表大小
    size_t iterations = 100;      // 合并操作次数

    list<int> main_list = generate_sorted_list(initial_size, min_val, max_val);
    // 启动 Gem5 统计
    // m5_checkpoint(0, 0);
    // m5_reset_stats(0, 0);

    test_merge_operations(main_list, initial_size, min_val, max_val, merge_size, iterations);

    // m5_dump_stats(0, 0);

    return 0;
}
*/

#include <list>
#include <random>
#include <iostream>
#include "/home/luoqiang/xymc/gem5_dda/include/gem5/m5ops.h"

using namespace std;

// 随机生成一个包含指定数量元素的列表，元素值在 [min, max] 范围内
list<int> generate_random_list(size_t size, int min, int max) {
    list<int> lst;
    random_device rd;  // 随机数种子
    mt19937 gen(rd()); // 随机数引擎
    uniform_int_distribution<> dis(min, max); // 均匀分布

    for (size_t i = 0; i < size; ++i) {
        lst.push_back(dis(gen)); // 生成随机数并添加到列表
    }
    return lst;
}

// 随机删除 y 个元素
void random_remove(list<int>& lst, size_t remove_count, int min_value, int max_value) {
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(min_value, max_value);

    for (size_t i = 0; i < remove_count; ++i) {
        int value_to_remove = dis(gen); // 随机选择一个要删除的值
        lst.remove(value_to_remove);    // 使用 list::remove 删除指定值
    }
}

// 随机插入 z 个元素
void random_insert(list<int>& lst, size_t insert_count, int min_value, int max_value) {
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(min_value, max_value);

    for (size_t i = 0; i < insert_count; ++i) {
        int value_to_insert = dis(gen); // 随机选择一个要插入的值
        lst.push_back(value_to_insert); // 插入到链表尾部
    }
}

// 主测试函数
void test_list_operations(list<int>& lst, size_t initial_size, int min_value, int max_value,
                            size_t remove_count, size_t insert_count, size_t iterations) {

    for (size_t i = 0; i < iterations; ++i) {
        random_remove(lst, remove_count, min_value, max_value);
        random_insert(lst, insert_count, min_value, max_value);
    }
}

int main() {
    // 参数设置
    size_t initial_size = 100000; // 初始链表大小
    int min_value = 1;           // 随机数最小值
    int max_value = 1000;         // 随机数最大值
    size_t remove_count = 100;     // 每次删除的元素数量
    size_t insert_count = 100;     // 每次插入的元素数量
    size_t iterations = 50;      // 重复删除和插入的次数

    list<int> lst1 = generate_random_list(initial_size, min_value, max_value);
    test_list_operations(lst1, initial_size, min_value, max_value,
                            remove_count, insert_count, iterations);
    list<int> lst2 = generate_random_list(initial_size, min_value, max_value);
    test_list_operations(lst2, initial_size, min_value, max_value,
                            remove_count, insert_count, iterations);
    lst1.sort();
    lst2.sort();
    m5_checkpoint(0, 0);
    m5_reset_stats(0, 0);
    lst1.merge(lst2);
    m5_dump_stats(0, 0);

    return 0;
}

