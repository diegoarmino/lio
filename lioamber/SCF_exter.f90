!%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!
!%% SCF_EXTER.F90 %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!
! File SCF_exter.f90 contains SCF setup and calls when used in tantem with     !
! AMBER or GROMACS. Routines included:                                         !
! * SCF_in  (called from AMBER)                                                !
! * SCF_gro (called from GROMACS)                                              !
!%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!

!%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!
!%% SCF_in %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!
! Performs SCF setup and routine calls from AMBER.                             !
!%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!
      subroutine SCF_in(E,qmcoords,clcoords,nsolin,dipxyz)
      use garcha_mod, only : r, v, Em, Rm, pc, natom, ntatom, nsol, rqm, &
                             writexyz, Iz

          implicit none
          real*8 , intent(in) :: qmcoords(3,natom), clcoords(4,nsolin)
          integer, intent(in) :: nsolin

          real*8 :: E, dipxyz(3)
          integer :: i, j, n

          nsol = nsolin ; ntatom = nsol + natom ;

          call g2g_timer_sum_start("Total")

          deallocate (r, v, Em, Rm, pc)
          allocate (r(ntatom, 3), v(ntatom, 3), Em(ntatom), Rm(ntatom), &
                    pc(ntatom))

          ! This section converts the coordinates array and partial charges    !
          ! array received from Gromacs into the r (all positions), rqm (QM    !
          ! region positions) and pc (MM partial charges) arrays. Also the xyz !
          ! file containing the QM region is written.                          ! 
          if(writexyz) write(18,*) natom
          !if(writexyz) write(18,*) natom
          if(writexyz) write(18,*)

          do i=1,natom
              do j=1,3
                 r(i,j)  = qmcoords(j,i) /0.529177D0
                 rqm(i,j)= qmcoords(j,i) /0.529177D0
              enddo
              if(writexyz) write(18,345) Iz(i), rqm(i,:)*0.529
          enddo

          do i=1,nsol
              n = natom + i
              pc(n) = clcoords(4,i)
              do j=1,3
                  r(n,j) = clcoords(j,i) / 0.529177D0
              enddo
              !if(writexyz) write(18,346) pc(n), r(n,:)*0.529
          enddo

           ! Calls liomain, which performs common procedures and SCF.
           call liomain(E, dipxyz)

 345  format(2x, I2,    2x, 3(f10.6,2x))
 346  format(2x, f10.6, 2x, 3(f10.6,2x))

      return
      end subroutine SCF_in
!%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!

!%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!
!%% SCF_GRO %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!
! Performs SCF setup and routine calls from GROMACS.                           !
!%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!
      subroutine SCF_gro(E, qmcoords, clcoords, clcharge, nsolin)
      use garcha_mod, only : nsol, ntatom, natom, r, v, Em, Rm, pc, nnat, &
                             writexyz, rqm, Iz

          implicit none
          real*8 , intent(in) :: qmcoords(3*natom), clcoords(3*nsolin), &
                                 clcharge(nsolin )

          real*8  :: dipxyz(3), E
          integer :: i, j, n, nsolin


          dipxyz = 0d0 ; nsol = nsolin ; ntatom = nsol + natom ;

          call g2g_timer_sum_start("Total")

          deallocate (r, v, Em, Rm, pc)
          allocate (r(ntatom,3), v(ntatom,3), Em(ntatom), Rm(ntatom), &
                    pc(ntatom))

          ! This section converts the coordinates array and partial charges    !
          ! array received from Gromacs into the r (all positions), rqm (QM    !
          ! region positions) and pc (MM partial charges) arrays. Also the xyz !
          ! file containing the QM region is written.                          !
          if(writexyz) write(18,*) natom
          !if(writexyz) write(18,*) ntatom
          if(writexyz) write(18,*)

          do i = 1, natom
              do j = 1, 3
                  r(i,j)  = qmcoords((i-1)*3+j)
                  rqm(i,j)= qmcoords((i-1)*3+j)
              enddo
              if(writexyz) write(18,345) Iz(i), rqm(i,:)*0.529
          enddo

          do i = 1, nsol
              n     = natom+i
              pc(n) = clcharge(i)
              do j = 1, 3
                  r(n, j) = clcoords((i-1)*3+j)
              enddo
              !if(writexyz) write(18,346) pc(n), r(n,:)*0.529
          enddo

          ! Calls liomain, which performs common procedures and SCF.
          call liomain(E, dipxyz)

 345  format(2x, I2,    2x, 3(f10.6,2x))
 346  format(2x, f10.6, 2x, 3(f10.6,2x))

      return
      end subroutine
!%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%!