cmake_minimum_required(VERSION 3.10)

add_executable(pekwm_lock pekwm_lock.cc)
target_include_directories(pekwm_lock PUBLIC ${common_INCLUDE_DIRS}
			   ${LIBSYSTEMD_INCLUDE_DIRS} ..)
target_link_libraries(pekwm_lock tk lib ${common_LIBRARIES} ${PAM_LIBRARIES}
		      ${LIBSYSTEMD_LIBRARIES})
install(TARGETS pekwm_lock DESTINATION bin)
