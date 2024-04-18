cd /home/dim8art/klee_build/klee_build140bitwuzla_stp_z3
cmake --build . -j8
cd /home/dim8art/klee/klee
rm -rf /home/dim8art/klee_build/klee_build140bitwuzla_stp_z3/test/Solver/Output/CexCacheValidityCoresCheck.c.tmp2.klee-out
rm -rf /home/dim8art/klee_build/klee_build140bitwuzla_stp_z3/test/Solver/Output/CexCacheValidityCoresCheck.c.tmp3.klee-out

/home/dim8art/klee_build/klee_build140bitwuzla_stp_z3/bin/klee --output-dir=/home/dim8art/klee_build/klee_build140bitwuzla_stp_z3/test/Solver/Output/CexCacheValidityCoresCheck.c.tmp2.klee-out --use-guided-search=none --cex-cache-validity-cores=false --max-instructions=100000 --solver-backend=z3 /home/dim8art/klee_build/klee_build140bitwuzla_stp_z3/test/Solver/Output/CexCacheValidityCoresCheck.c.tmp1.bc
/home/dim8art/klee_build/klee_build140bitwuzla_stp_z3/bin/klee --output-dir=/home/dim8art/klee_build/klee_build140bitwuzla_stp_z3/test/Solver/Output/CexCacheValidityCoresCheck.c.tmp3.klee-out --seed-dir=/home/dim8art/klee_build/klee_build140bitwuzla_stp_z3/test/Solver/Output/CexCacheValidityCoresCheck.c.tmp2.klee-out --use-guided-search=none --prune-already-executed-states=true --cex-cache-validity-cores=true --solver-backend=z3 --use-seeded-search=true /home/dim8art/klee_build/klee_build140bitwuzla_stp_z3/test/Solver/Output/CexCacheValidityCoresCheck.c.tmp1.bc