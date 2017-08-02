! <testinfo>
! test_generator=(config/mercurium-ompss config/mercurium-ompss-v2)
! </testinfo>
PROGRAM P
    IMPLICIT NONE
    INTEGER, ALLOCATABLE :: X(:)

    ALLOCATE(X(1000))

    X = 0
    !$OMP TASK FIRSTPRIVATE(X)
        X = 1
    !$OMP END TASK
    !$OMP TASKWAIT
    IF (ANY(X /= 0)) STOP 1
END PROGRAM P
