! <testinfo>
! test_generator=(config/mercurium-ompss config/mercurium-ompss-v2)
! </testinfo>

PROGRAM P
    IMPLICIT NONE
    INTEGER :: I, LIMIT
        !$OMP TASK FINAL(.FALSE.)
        !$OMP END TASK

        !$OMP TASK FINAL(I < LIMIT)
        !$OMP END TASK

        !$OMP TASKWAIT
END PROGRAM P
