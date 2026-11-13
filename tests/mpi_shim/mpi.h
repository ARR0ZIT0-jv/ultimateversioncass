#ifndef MPI_SHIM_H
#define MPI_SHIM_H
// ============================================================================================
//  SHIM DE PRUEBAS - NO ES MPI REAL.
//  Implementa solo las llamadas que usa este proyecto, con PROCESOS reales (fork) conectados por
//  socketpairs. Sirve para ejecutar y verificar el protocolo Master/Worker en una maquina sin
//  OpenMPI. El binario de produccion se compila con `mpicxx` + OpenMPI (make mpi), NO con esto.
//  Uso:  make test_mpi_shim     (numero de procesos: variable de entorno MPI_SHIM_NP, defecto 3)
// ============================================================================================
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

typedef int MPI_Comm;
typedef int MPI_Datatype;
#define MPI_COMM_WORLD 0
#define MPI_BYTE 1
#define MPI_UINT64_T 3
#define MPI_THREAD_FUNNELED 1
struct MPI_Status { int MPI_SOURCE; int MPI_TAG; };

namespace mpishim {
inline size_t dt(MPI_Datatype d) { return d == MPI_UINT64_T ? 8 : 1; }
struct Msg { int tag; std::vector<char> data; };
struct State {
    int rank = 0, size = 1;
    std::vector<int> fd;
    std::vector<std::deque<Msg>> pending;
    std::vector<pid_t> children;
};
inline State& S() { static State s; return s; }
inline void wr(int fd, const void* p, size_t n) {
    const char* c = static_cast<const char*>(p);
    while (n) { ssize_t k = ::write(fd, c, n); if (k <= 0) { perror("shim write"); _exit(99); } c += k; n -= static_cast<size_t>(k); }
}
inline void rd(int fd, void* p, size_t n) {
    char* c = static_cast<char*>(p);
    while (n) { ssize_t k = ::read(fd, c, n); if (k <= 0) { fprintf(stderr, "shim: conexion cerrada\n"); _exit(98); } c += k; n -= static_cast<size_t>(k); }
}
inline void send_msg(const void* buf, size_t n, int dst, int tag) {
    int hdr[2] = {tag, static_cast<int>(n)};
    wr(S().fd[dst], hdr, sizeof(hdr));
    if (n) wr(S().fd[dst], buf, n);
}
inline void recv_msg(void* buf, size_t n, int src, int tag) {
    auto& q = S().pending[src];
    for (auto it = q.begin(); it != q.end(); ++it)
        if (it->tag == tag) { if (it->data.size() != n) { fprintf(stderr, "shim: tamano inesperado tag %d (%zu != %zu)\n", tag, it->data.size(), n); _exit(97); }
                              { if (n) memcpy(buf, it->data.data(), n); } q.erase(it); return; }
    for (;;) {
        int hdr[2]; rd(S().fd[src], hdr, sizeof(hdr));
        Msg m; m.tag = hdr[0]; m.data.resize(static_cast<size_t>(hdr[1]));
        if (hdr[1]) rd(S().fd[src], m.data.data(), m.data.size());
        if (m.tag == tag) { if (m.data.size() != n) { fprintf(stderr, "shim: tamano inesperado tag %d\n", tag); _exit(97); }
                            { if (n) memcpy(buf, m.data.data(), n); } return; }
        q.push_back(std::move(m));
    }
}
} // namespace mpishim

inline int MPI_Init_thread(int*, char***, int, int* provided) {
    auto& s = mpishim::S();
    const char* e = getenv("MPI_SHIM_NP");
    s.size = e ? atoi(e) : 3;
    if (provided) *provided = MPI_THREAD_FUNNELED;
    std::vector<std::vector<int>> sp(static_cast<size_t>(s.size) * s.size * 2, std::vector<int>());
    std::vector<int> fds(static_cast<size_t>(s.size) * s.size * 2, -1);   // [i][j][0/1]
    for (int i = 0; i < s.size; ++i) for (int j = i + 1; j < s.size; ++j) {
        int p[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, p);
        fds[(static_cast<size_t>(i) * s.size + j) * 2] = p[0];
        fds[(static_cast<size_t>(i) * s.size + j) * 2 + 1] = p[1];
    }
    s.rank = 0;
    fflush(stdout); fflush(stderr);
    for (int r = 1; r < s.size; ++r) {
        pid_t pid = fork();
        if (pid == 0) { s.rank = r; s.children.clear(); break; }
        s.children.push_back(pid);
    }
    s.fd.assign(static_cast<size_t>(s.size), -1);
    for (int peer = 0; peer < s.size; ++peer) {
        if (peer == s.rank) continue;
        const int a = std::min(s.rank, peer), b = std::max(s.rank, peer);
        s.fd[peer] = fds[(static_cast<size_t>(a) * s.size + b) * 2 + (s.rank == a ? 0 : 1)];
    }
    s.pending.assign(static_cast<size_t>(s.size), {});
    return 0;
}
inline int MPI_Comm_rank(MPI_Comm, int* r) { *r = mpishim::S().rank; return 0; }
inline int MPI_Comm_size(MPI_Comm, int* n) { *n = mpishim::S().size; return 0; }
inline int MPI_Send(const void* b, int n, MPI_Datatype d, int dst, int tag, MPI_Comm) { mpishim::send_msg(b, static_cast<size_t>(n) * mpishim::dt(d), dst, tag); return 0; }
inline int MPI_Recv(void* b, int n, MPI_Datatype d, int src, int tag, MPI_Comm, MPI_Status* st) {
    mpishim::recv_msg(b, static_cast<size_t>(n) * mpishim::dt(d), src, tag);
    if (st) { st->MPI_SOURCE = src; st->MPI_TAG = tag; }
    return 0;
}
inline int MPI_Sendrecv(const void* sb, int sn, MPI_Datatype sd, int dst, int stag, void* rb, int rn, MPI_Datatype rd_, int src, int rtag, MPI_Comm, MPI_Status* st) {
    std::thread t([&] { mpishim::send_msg(sb, static_cast<size_t>(sn) * mpishim::dt(sd), dst, stag); });
    mpishim::recv_msg(rb, static_cast<size_t>(rn) * mpishim::dt(rd_), src, rtag);
    t.join();
    if (st) { st->MPI_SOURCE = src; st->MPI_TAG = rtag; }
    return 0;
}
inline int MPI_Bcast(void* b, int n_, MPI_Datatype d, int root, MPI_Comm) {
    auto& s = mpishim::S();
    const int n = static_cast<int>(static_cast<size_t>(n_) * mpishim::dt(d));
    if (s.rank == root) { for (int r = 0; r < s.size; ++r) if (r != root) mpishim::send_msg(b, static_cast<size_t>(n), r, -1); }
    else mpishim::recv_msg(b, static_cast<size_t>(n), root, -1);
    return 0;
}
inline int MPI_Barrier(MPI_Comm) {
    auto& s = mpishim::S();
    char c = 0;
    if (s.rank == 0) { for (int r = 1; r < s.size; ++r) mpishim::recv_msg(&c, 1, r, -2); for (int r = 1; r < s.size; ++r) mpishim::send_msg(&c, 1, r, -3); }
    else { mpishim::send_msg(&c, 1, 0, -2); mpishim::recv_msg(&c, 1, 0, -3); }
    return 0;
}
inline double MPI_Wtime() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
inline int MPI_Abort(MPI_Comm, int code) { fflush(stdout); fflush(stderr); _exit(code); }
inline int MPI_Finalize() {
    auto& s = mpishim::S();
    fflush(stdout); fflush(stderr);
    if (s.rank == 0) for (pid_t p : s.children) { int st; waitpid(p, &st, 0); }
    return 0;
}
#endif
