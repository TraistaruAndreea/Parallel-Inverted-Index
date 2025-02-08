#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <numeric>
#include <pthread.h>
#include <atomic>

#define NUM_LETTERS 26

struct WordEntry {
    std::string word;
    std::vector<int> file_ids;
};

struct ResultData {
    std::unordered_map<std::string, WordEntry> table;
    pthread_mutex_t lock;
};

struct ThreadData {
    std::queue<int> file_queue;
    pthread_mutex_t queue_lock;
    ResultData results[NUM_LETTERS];
    std::atomic<int> active_mappers;
    std::atomic<int> mapper_finished_count;
    pthread_barrier_t barrier; 
};

struct MapperArgs {
    ThreadData* data;
    std::vector<std::string>* files;
};

struct ReducerArgs {
    ThreadData* data;
    int reducer_id;
    int num_reducers;
};

void clean_word(std::string &word) {
    word.erase(std::remove_if(word.begin(), word.end(), [](char c) {
        return !std::isalpha(c);
    }), word.end());
    std::transform(word.begin(), word.end(), word.begin(), ::tolower);
}

void add_table(ResultData &result, const std::string &word, int file_id) {

    // Lock the shared result
    pthread_mutex_lock(&result.lock);

    // Add the word to the shared result if it exists
    if (result.table.find(word) == result.table.end()) {
        result.table[word] = {word, {file_id + 1}};
    } else {

        // Add the file id to the word's list of file ids
        auto &entry = result.table[word];
        int file_index = file_id + 1;

        // Find the position where the file id should be inserted
        auto it = std::lower_bound(entry.file_ids.begin(), entry.file_ids.end(), file_index);
        if (it == entry.file_ids.end() || *it != file_index) {
            entry.file_ids.insert(it, file_index);
        }
    }
    pthread_mutex_unlock(&result.lock);
}

void* mapper(void* arg) {
    auto* args = static_cast<MapperArgs*>(arg);
    auto* data = args->data;
    auto& files = *args->files;

    while (true) {
        int file_id = -1;

        // Lock the access to the file queue
        pthread_mutex_lock(&data->queue_lock);

        // Get the next file id from the queue
        if (!data->file_queue.empty()) {
            file_id = data->file_queue.front();
            data->file_queue.pop();
        }

        pthread_mutex_unlock(&data->queue_lock);

        if (file_id == -1) break;

        std::ifstream file(files[file_id]);
        if (!file.is_open()) {
            std::cerr << "Error opening file: " << files[file_id] << std::endl;
            continue;
        }

        // Read the words from the file and store the results in a local map
        std::unordered_map<int, std::unordered_map<std::string, int>> local_results;
        std::string word;

        while (file >> word) {
            clean_word(word);
            if (!word.empty()) {
                int letter_index = word[0] - 'a';
                if (letter_index >= 0 && letter_index < NUM_LETTERS) {
                    local_results[letter_index][word]++;
                }
            }
        }

        // Update shared results
        for (const auto &bucket : local_results) {
            int letter_index = bucket.first;
            for (const auto &entry : bucket.second) {
                add_table(data->results[letter_index], entry.first, file_id);
            }
        }
    }

    pthread_barrier_wait(&data->barrier);
    delete args;
    return nullptr;
}

void* reducer(void* arg) {
    auto* args = static_cast<ReducerArgs*>(arg);
    auto* data = args->data;
    int reducer_id = args->reducer_id;
    int num_reducers = args->num_reducers;

    // Wait for all mappers to finish
    pthread_barrier_wait(&data->barrier);

    // Divide the letters between reducers
    int start_index = reducer_id * NUM_LETTERS / num_reducers;
    int end_index = std::min(NUM_LETTERS, (reducer_id + 1) * NUM_LETTERS / num_reducers);

    for (int letter_index = start_index; letter_index < end_index; letter_index++) {
        std::vector<WordEntry> words_array;

        // Lock the shared result
        pthread_mutex_lock(&data->results[letter_index].lock);

        for (const auto &entry : data->results[letter_index].table) {
            words_array.push_back(entry.second);
        }

        pthread_mutex_unlock(&data->results[letter_index].lock);

        // Sort the words by the number of file ids and then by the word itself
        std::sort(words_array.begin(), words_array.end(), [](const WordEntry &a, const WordEntry &b) {
            if (a.file_ids.size() != b.file_ids.size())
                return a.file_ids.size() > b.file_ids.size();
            return a.word < b.word;
        });

        // Write the results to the output file
        std::ofstream output(std::string(1, 'a' + letter_index) + ".txt");
        if (!output.is_open()) {
            std::cerr << "Error creating output file for letter " << char('a' + letter_index) << std::endl;
            delete args;
            return nullptr;
        }

        for (const auto &entry : words_array) {
            output << entry.word << ":[" << std::accumulate(entry.file_ids.begin(), entry.file_ids.end(), std::string(),
                                                             [](const std::string &a, int b) {
                                                                 return a.empty() ? std::to_string(b) : a + " " + std::to_string(b);
                                                             }) << "]\n";
        }
    }

    delete args;
    return nullptr;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <nr_mappers> <nr_reducers> <input_file>" << std::endl;
        return 1;
    }

    int nr_mappers = std::stoi(argv[1]);
    int nr_reducers = std::stoi(argv[2]);
    std::string input_file = argv[3];

    std::ifstream file(input_file);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << input_file << std::endl;
        return 1;
    }

    int nr_files;
    file >> nr_files;

    ThreadData thread_data;
    thread_data.active_mappers = nr_mappers;
    thread_data.mapper_finished_count = 0;

    // Read the file names and add them to the file queue
    std::vector<std::string> files(nr_files);
    for (int i = 0; i < nr_files; i++) {
        file >> files[i];
        thread_data.file_queue.push(i);
    }

    // Initialize the barrier
    pthread_barrier_init(&thread_data.barrier, nullptr, nr_mappers + nr_reducers);

    // Initialize the mutexes
    pthread_mutex_init(&thread_data.queue_lock, nullptr);
    for (int i = 0; i < NUM_LETTERS; i++) {
        pthread_mutex_init(&thread_data.results[i].lock, nullptr);
    }

    pthread_t threads[nr_mappers + nr_reducers];

    // Create the mapper and reducers threads
    for (int i = 0; i < nr_mappers + nr_reducers; i++) {
        if (i < nr_mappers) {
            MapperArgs* args = new MapperArgs{&thread_data, &files};
            pthread_create(&threads[i], nullptr, mapper, args);
        } else {
            ReducerArgs* args = new ReducerArgs{&thread_data, i - nr_mappers, nr_reducers};
            pthread_create(&threads[i], nullptr, reducer, args);
        }
            
    }

    for (int i = 0; i < nr_mappers + nr_reducers; ++i) {
        pthread_join(threads[i], nullptr);
    }

    pthread_barrier_destroy(&thread_data.barrier);
    pthread_mutex_destroy(&thread_data.queue_lock);
    for (int i = 0; i < NUM_LETTERS; i++) {
        pthread_mutex_destroy(&thread_data.results[i].lock);
    }

    return 0;
}
