# Book Borrow System

A console-based Book Borrow Management System developed using the C programming language as a Data Structures project.

## Data Structures Used

- **Hash Table (Chaining):** Stores book and student records.
- **Min Heap:** Organizes active loans based on due dates.
- **Queue (Linked List):** Maintains FIFO book reservation waitlists.
- **Linked List:** Stores returned book transaction history.

## Features

1. Add new books.
2. Register students.
3. Issue books.
4. Return books.
5. Calculate late return fines.
6. Display available books.
7. View active loans.
8. Display overdue books.
9. View borrowing history.

## Technologies Used

- Programming Language: C
- Compiler: GCC
- Platform: Windows / Linux / macOS

## How to Run

### Windows

```bash
gcc library_system.c -o library.exe
library.exe
```

### Linux / macOS

```bash
gcc library_system.c -o library
./library
```

## Sample Data

**Books**
- B1 – Data Structures in C
- B2 – Algorithms

**Students**
- S1 – Asha
- S2 – Ravi

## Project Structure

```text
Book-Borrow-System/
│
├── library_system.c
└── README.md
```

## Author

Adheep XHyper

## Project Type

Data Structures Project
