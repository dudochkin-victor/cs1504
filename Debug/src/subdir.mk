################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/crccalc.cpp \
../src/cs1504.cpp \
../src/csp2.cpp 

OBJS += \
./src/crccalc.o \
./src/cs1504.o \
./src/csp2.o 

CPP_DEPS += \
./src/crccalc.d \
./src/cs1504.d \
./src/csp2.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


