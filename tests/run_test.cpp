#include <fastenvelope/FastEnvelope.h>
#include <fastenvelope/utils/csv_reader.h>
#include <fastenvelope/Types.hpp>
#include <fastenvelope/utils/getRSS.hpp>

#include <catch2/catch_all.hpp>

#ifdef USE_TBB
#include <tbb/tbb.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fastEnvelope;

inline bool read_stl_binary(
    std::istream& input,
    std::vector<Vector3>& vertices,
    std::vector<Vector3i>& faces)
{
    if (!input) {
        throw std::runtime_error("Failed to open file");
    }

    static_assert(sizeof(float) == 4, "Binary STL requires 32-bit floats");

    // 80 bytes header, no data significance.
    std::array<char, 80> header;
    input.read(header.data(), header.size());
    if (!input) {
        throw std::runtime_error("Unable to parse STL header.");
    }

    std::uint32_t num_faces = 0;
    input.read(reinterpret_cast<char*>(&num_faces), sizeof(num_faces));
    if (!input) {
        throw std::runtime_error("Unable to parse STL number of faces.");
    }

    vertices.reserve(vertices.size() + 3 * num_faces);
    faces.reserve(faces.size() + num_faces);
    for (std::uint32_t i = 0; i < num_faces; ++i) {
        // A facet contains a normal followed by its three vertices.
        std::array<float, 12> facet;
        std::uint16_t attribute = 0;
        input.read(reinterpret_cast<char*>(facet.data()), sizeof(facet));
        input.read(reinterpret_cast<char*>(&attribute), sizeof(attribute));
        if (!input) {
            throw std::runtime_error("Failed to parse STL facet " + std::to_string(i));
        }

        const auto first_vertex = static_cast<int>(vertices.size());
        for (int vertex = 0; vertex < 3; ++vertex) {
            const int offset = 3 + 3 * vertex;
            Vector3 position(facet[offset], facet[offset + 1], facet[offset + 2]);
            if (!position.allFinite()) {
                throw std::runtime_error("NaN or Inf detected in input file.");
            }
            vertices.push_back(position);
        }
        faces.emplace_back(first_vertex, first_vertex + 1, first_vertex + 2);
    }

    return true;
}

inline bool read_stl_binary(
    const std::string& filename,
    std::vector<Vector3>& vertices,
    std::vector<Vector3i>& faces)
{
    std::ifstream input(filename, std::ios::binary);
    return read_stl_binary(input, vertices, faces);
}

void pure_our_method(
    string queryfile,
    string model,
    string resultfile,
    Scalar envsize,
    bool csv_model)
{
    std::cout << "running our method" << std::endl;
    vector<int> outenvelope;
    std::vector<Vector3> env_vertices, v;
    std::vector<Vector3i> env_faces, f;

    std::vector<std::array<Vector3, 3>> triangles;
    if (csv_model) {
        triangles = read_CSV_triangle(queryfile, outenvelope);
    } else {
        std::ifstream input(queryfile, std::ios::binary);
        bool ok1 = read_stl_binary(input, v, f);
        if (!ok1) {
            std::cout << ("Unable to load query mesh") << std::endl;
            REQUIRE(false);
            return;
        }
        triangles.resize(f.size());
        for (size_t i = 0; i < f.size(); ++i) {
            triangles[i] = {{v[f[i][0]], v[f[i][1]], v[f[i][2]]}};
        }
    }

    bool ok = read_stl_binary(model, env_vertices, env_faces);
    if (!ok) {
        std::cout << ("Unable to load mesh") << std::endl;
        return;
    }

    Vector3 min, max;
    Scalar eps = envsize;

    Scalar dd;
    algorithms::get_bb_corners(env_vertices, min, max);
    dd = ((max - min).norm()) * eps;

    const FastEnvelope fast_envelope(env_vertices, env_faces, dd);

    // int fn = triangles.size() > 100000 ? 100000 : triangles.size();
    int fn = triangles.size() > 100000 ? 100000 : triangles.size();
    std::cout << "total query size, " << fn << std::endl;
    std::vector<bool> results;
    results.resize(fn);
    int inbr = 0;
    for (int i = 0; i < fn; i++) {
        results[i] = fast_envelope.is_outside(triangles[i]);
        if (results[i] == 0) inbr++;
    }
    cout << "memory use, " << getPeakRSS() << std::endl;
    cout << "inside percentage, " << float(inbr) / float(fn) << std::endl;
    std::cout << "inside number, " << inbr << std::endl;
    if (inbr == 97688) {
        std::cout << "\n\n\nthe results are correct\n\n\n" << std::endl;
    } else {
        std::cout << "\n\n\nTHIS TEST IS WRONG! the number of queries that are inside envelope "
                     "should be 97688\n\n\n"
                  << std::endl;
        throw "\n\n\nWRONG ANSWERS\n\n\n";
    }
    std::ofstream fout;
    fout.open(resultfile + ".json");
    fout << "{\n";

    fout << "\"method\": " << "\"ours\"" << ",\n";
    fout << "\"memory\": " << getPeakRSS() << ",\n";
    fout << "\"inside\": " << double(inbr) / double(fn) << ",\n";
    fout << "\"queries\": " << fn << ",\n";
    fout << "\"vertices\": " << env_vertices.size() << ",\n";
    fout << "\"facets\": " << env_faces.size() << "\n";
    fout << "}";
    fout.close();
    fast_envelope.printnumber();


    fout.open(resultfile);
    fout << "results" << endl;
    for (int i = 0; i < fn; i++) {
        fout << results[i] << endl;
    }
    fout.close();
    std::cout << model << " done! " << std::endl;
}

void test_initialization(const std::string& filename)
{
    std::vector<Vector3> env_vertices;
    std::vector<Vector3i> env_faces;
    bool ok = read_stl_binary(filename, env_vertices, env_faces);
    if (!ok) {
        std::cout << ("Unable to load mesh") << std::endl;
        return;
    }

    Vector3 min, max;
    Scalar eps = 1e-3;

    Scalar dd;
    algorithms::get_bb_corners(env_vertices, min, max);
    dd = ((max - min).norm()) * eps;
    std::cout << "bounding box \nmin " << min.transpose() << "\nmax" << max.transpose()
              << std::endl;
    const FastEnvelope fast_envelope(env_vertices, env_faces, dd);
}

TEST_CASE("Run mini benchmark", "[integration]")
{
#ifdef ENVELOPE_WITH_GMP
    std::cout << "using RATIONAL calculation in GMP" << std::endl;
#endif

    /*const string filename = "D:\\vs/envelope_x64\\Debug\\M-L-Femur.stl";
    test_initialization(filename);*/
    std::string datapath = ENVELOPE_TEST_DATA_DIR;
    std::cout << "Running the test dataset.\ndata path," << datapath << std::endl;
    string query = datapath + "63465.stl_envelope_log.csv";
    string model = datapath + "63465.stl";
    string resultfile = datapath + "63465_result.csv";
    Scalar scale_ratio = 1e-3;
    bool csv_model = true;
    pure_our_method(query, model, resultfile, scale_ratio, csv_model);
}
