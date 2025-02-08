# Parallel Inverted Index using Map-Reduce (Pthreads)

## Overview
This project implements a **parallel inverted index** using the **Map-Reduce** paradigm with **Pthreads**. It processes multiple input files in parallel and generates an index listing all unique words along with the files in which they appear. The implementation ensures efficient concurrent processing by using **thread synchronization** via mutexes and barriers.

## Features
- **Parallel Processing**: Uses multiple threads for efficient file processing.
- **Map-Reduce Architecture**:
  - **Mapper Threads**: Read and extract words from input files.
  - **Reducer Threads**: Aggregate and sort words, then output results.
- **Thread Synchronization**: Uses mutexes for shared data protection and barriers for synchronization.
- **Alphabetical Indexing**: Outputs results in separate files for each letter of the alphabet.

## Code Structure
- **`mapper()`**: Reads files, extracts words, and stores results.
- **`reducer()`**: Aggregates words and writes output.
- **`clean_word()`**: Cleans and normalizes words.
- **`add_table()`**: Stores words with associated file IDs.
- **`ThreadData` & `ResultData`**: Structures for shared data management.

## Thread Synchronization
- **Mutexes**: Protect shared result tables.
- **Barrier**: Ensures all mappers finish before reducers start.


