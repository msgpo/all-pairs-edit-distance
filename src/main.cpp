/* main.c
 *
 * Copyright 2019 Giovanni Campagna
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "all-pairs-edit-distance-config.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace aped {

class Progbar {
    size_t current = 0;
    const size_t total;
    const std::chrono::time_point<std::chrono::steady_clock> start_time;

    std::stringstream buffer;
    size_t current_size = 0;
    static const size_t LENGTH = 40;

    void print() {
        size_t mod = std::max(1UL, total / 100000);
        if (current % mod != 0)
            return;

        // clear
        for (size_t i = 0; i < current_size; i++)
            buffer << "\b";
        buffer << "\r";
        std::cerr << buffer.str();
        buffer.str("");

        double pct = double(current * 100) / total;
        size_t length = current * LENGTH / total;

        current_size = 0;
        buffer << " " <<
            std::setw(5) << pct << std::setw(0) << " [";
        for (size_t i = 0; i < length; i++)
            buffer << "=";
        if (current < total) {
            buffer << ">";
            for (size_t i = length + 1; i < LENGTH; i++)
                buffer << " ";
            buffer << "] - ETA: ";

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1>>>(now - start_time);
            auto speed = double(current) / elapsed.count();
            auto eta = (total - current) / speed;
            buffer << eta << " s";

            auto str = buffer.str();
            current_size = str.size();
            std::cerr << str;
        } else {
            buffer << "]\n";
            std::cerr << buffer.str();
        }

        buffer.str("");
    }

public:
    Progbar(size_t _total) : total(_total), start_time(std::chrono::steady_clock::now()) {
        buffer << std::fixed << std::setprecision(1);

        print();
    }

    void inc() {
        if (current >= total)
            return;
        current += 1;
        print();
    }
};


template<typename T>
class Matrix {
    std::unique_ptr<T[]> buffer;
public:
    const size_t R;
    const size_t C;

public:
    Matrix(size_t r, size_t c) : buffer(new T[r * c]), R(r), C(c) {}

    T& at(size_t i, size_t j) {
        return buffer[i * C + j];
    }
    const T& at(size_t i, size_t j) const {
        return buffer[i * C + j];
    }
};

template<typename Container>
size_t
slow_edit_distance(const Container& one, const Container& two)
{
    size_t len1 = one.size();
    size_t len2 = two.size();

    while (len1 > 0 && len2 > 0 && one[len1-1] == two[len2-1]) {
        len1--;
        len2--;
    }

    if (len1 == 0)
        return len2;
    if (len2 == 0)
        return len1;

    Matrix<size_t> matrix(len1+1, len2+1);

    matrix.at(0, 0) = 0;
    for (size_t j = 1; j <= len2; j++)
        matrix.at(0, j) = j;

    for (size_t i = 1; i <= len1; i++) {
        matrix.at(i, 0) = i;
        for (size_t j = 1; j <= len2; j++) {
            if (one[i-1] == two[j-1])
                matrix.at(i, j) = matrix.at(i-1, j-1);
            else
                matrix.at(i, j) = 1 + std::min(matrix.at(i-1, j-1), std::min(matrix.at(i-1, j), matrix.at(i, j-1)));
        }
    }

    return matrix.at(len1, len2);
}

template<typename StringLike>
std::vector<std::string_view>
string_split(const StringLike& from, char c)
{
    std::vector<std::string_view> output;

    size_t current = 0;
    size_t next = from.find(c, current);
    while (next != StringLike::npos) {
        output.push_back(std::string_view(from.data() + current, next - current));
        current = next + 1;
        next = from.find(c, current);
    }
    if (current != from.size())
        output.push_back(std::string_view(from.data() + current, from.size() - current));

    return output;
}

typedef std::vector<std::string_view> Field;
struct Line {
    const std::string_view id;
    std::vector<Field> fields;
};

static std::pair<std::vector<std::string>, std::vector<Line>>
load(const char *filename)
{
    // buffer all the lines in memory, because we're going to mess with string_views
    std::vector<std::string> buffer;
    std::vector<Line> lines;

    std::ifstream fp(filename);
    std::string line;

    ssize_t num_fields(-1);

    while (!fp.eof()) {
        std::getline(fp, line);
        if (line == "")
            continue;
        buffer.emplace_back(std::move(line));

        auto chunks = string_split(buffer.back(), '\t');
        if (chunks.size() <= 1) {
            std::cerr << "malformed line: " << buffer.back() << std::endl;
            continue;
        }

        Line parsed { chunks[0] };
        if (num_fields < 0) {
            num_fields = chunks.size() - 1;
        } else if (chunks.size() - 1 != (size_t)num_fields) {
            std::cerr << "malformed line: " << buffer.back() << std::endl;
            continue;
        }
        for (size_t i = 1; i < chunks.size(); i++)
            parsed.fields.emplace_back(string_split(chunks[i], ' '));

        lines.emplace_back(std::move(parsed));
    }

    return std::make_pair(std::move(buffer), std::move(lines));
}

std::mutex output_lock;
static void
process(std::ostream& fout, Progbar& progbar, const Line& one, const Line& two) {
    size_t num_fields = one.fields.size();

    std::vector<size_t> eds;
    eds.reserve(num_fields);
    for (size_t field = 0; field < num_fields; field++)
        eds.push_back(slow_edit_distance(one.fields[field], two.fields[field]));

    {
        std::lock_guard<std::mutex> locker(output_lock);
        fout << one.id << "\t" << two.id;
        for (size_t ed : eds)
            fout << "\t" << ed;
        fout << "\n";
        progbar.inc();
    }
}

static void
run(const char *filename_in, const char *filename_out)
{
    auto loaded = load(filename_in);
    std::ofstream fout(filename_out);
    const auto& lines = loaded.second;
    Progbar progbar(lines.size() * (lines.size()-1) / 2);

    #pragma omp parallel for
    for (size_t i = 0; i < lines.size(); i++) {
        for (size_t j = i + 1; j < lines.size(); j++) {
            process(fout, progbar, lines[i], lines[j]);
        }
    }
}

}

static void
usage(const char *argv0)
{
    std::cerr << "usage: " << argv0 << " <input file> <output file> [<field>]" << std::endl;
}

int
main (int   argc,
      char *argv[])
{
    if (argc < 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp("--help", argv[1]) == 0 || strcmp("--help", argv[2]) == 0) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (strcmp("--version", argv[1]) == 0 || strcmp("--version", argv[2]) == 0) {
        std::cerr << argv[0] << " " << PACKAGE_VERSION << std::endl;
        return EXIT_SUCCESS;
    }

    aped::run(argv[1], argv[2]);

    return EXIT_SUCCESS;
}
