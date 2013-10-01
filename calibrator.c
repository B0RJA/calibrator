/*
Calibrator: a software to make calibrations of empirical parameters.

AUTHORS: Javier Burguete and Borja Latorre.

Copyright 2012-2013, AUTHORS.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright notice,
		this list of conditions and the following disclaimer.

	2. Redistributions in binary form must reproduce the above copyright notice,
		this list of conditions and the following disclaimer in the
		documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

/**
 * \file calibrator.c
 * \brief Source file of the calibrator.
 * \authors Javier Burguete and Borja Latorre.
 * \copyright Copyright 2013, all rights reserved.
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <gsl/gsl_rng.h>
#include <libxml/parser.h>
#include <glib.h>
#ifdef HAVE_MPI
	#include <mpi.h>
#endif

/**
 * \def DEBUG
 * \brief Macro to debug.
 */
#define DEBUG 0

/**
 * \enum CalibrateAlgorithm
 * \brief Enum to define the calibration algorithm.
 */
enum CalibrateAlgorithm
{
	CALIBRATE_ALGORITHM_MONTE_CARLO = 0,
	CALIBRATE_ALGORITHM_SWEEP = 1,
	CALIBRATE_ALGORITHM_GENETIC = 2
};

/**
 * \struct Calibrate
 * \brief Struct to define the calibration data.
 */
typedef struct
{
/**
 * \var simulator
 * \brief Name of the simulator program.
 * \var evaluator
 * \brief Name of the program to evaluate the objective function.
 * \var template1
 * \brief Array of 1st template names of input files.
 * \var template2
 * \brief Array of 2nd template names of input files.
 * \var template3
 * \brief Array of 3th template names of input files.
 * \var template4
 * \brief Array of 4th template names of input files.
 * \var experiment
 * \brief Array experimental data file names.
 * \var label
 * \brief Array of variable names.
 * \var format
 * \brief Array of variable formats.
 * \var simulation_best
 * \brief Number of the best simulation.
 * \var nvariables
 * \brief Variables number.
 * \var nexperiments
 * \brief Experiments number.
 * \var ninputs
 * \brief Number of input files to the simulator.
 * \var nsimulations
 * \brief Simulations number per experiment.
 * \var algorithm
 * \brief Algorithm number
 * \var nsweeps
 * \brief Array of sweeps of the sweep algorithm.
 * \var nstart
 * \brief Beginning simulation number of the task.
 * \var nend
 * \brief Ending simulation number of the task.
 * \var nthreads
 * \brief Number of threads.
 * \var thread
 * \brief Array of simulation numbers to calculate on the thread.
 * \var error_best
 * \brief Minimum error.
 * \var value
 * \brief Array of variable values.
 * \var rangemin
 * \brief Array of minimum variable values.
 * \var rangemax
 * \brief Array of maximum variable values.
 * \var rng
 * \brief Pseudo-random numbers generator struct.
 * \var mutex
 * \brief Mutex struct.
 * \var file1
 * \brief Array of 1st input template files.
 * \var file2
 * \brief Array of 2nd input template files.
 * \var file3
 * \brief Array of 3th input template files.
 * \var file4
 * \brief Array of 4th input template files.
 * \var mpi_rank
 * \brief Number of MPI task.
 * \var mpi_tasks
 * \brief Total number of MPI tasks.
 */
	char *simulator, *evaluator, **experiment, **template1, **template2,
		**template3, **template4, **label, **format;
	unsigned int simulation_best, nvariables, nexperiments, ninputs,
		nsimulations, algorithm, *nsweeps, nstart, nend, nthreads, *thread;
	double error_best, *value, *rangemin, *rangemax;
	gsl_rng *rng;
	GMutex mutex[1];
	GMappedFile **file1, **file2, **file3, **file4;
#ifdef HAVE_MPI
	int mpi_rank, mpi_tasks;
#endif
} Calibrate;

/**
 * \struct ParallelData
 * \brief Struct to pass to the GThreads parallelized function.
 */
typedef struct
{
/**
 * \var thread
 * \brief Thread number.
 * \var calibrate
 * \brief Calibration data pointer.
 */
	unsigned int thread;
	Calibrate *calibrate;
} ParallelData;

GMutex mutex;

void calibrate_template(Calibrate *calibrate, unsigned int simulation,
	char *input, GMappedFile *template)
{
	unsigned int i;
	char buffer[32], value[32], *buffer2, *buffer3, *content;
	FILE *file;
	gsize length;
	GRegex *regex;

#if DEBUG
printf("calibrate_template: start\n");
#endif

	// Opening template
	content = g_mapped_file_get_contents(template);
	length = g_mapped_file_get_length(template);
#if DEBUG
printf("calibrate_template: length=%lu\ncontent:\n%s", length, content);
#endif
	file = fopen(input, "w");

	// Parsing template
	for (i = 0; i < calibrate->nvariables; ++i)
	{
#if DEBUG
printf("calibrate_template: variable=%u\n", i);
#endif
		snprintf(buffer, 32, "@variable%u@", i + 1);
		regex = g_regex_new(buffer, 0, 0, NULL);
		if (i == 0)
		{
			buffer2 = g_regex_replace_literal(regex, content, length, 0,
				calibrate->label[i], 0, NULL);
#if DEBUG
printf("calibrate_template: buffer2\n%s", buffer2);
#endif
		}
		else
		{
			length = strlen(buffer3);
			buffer2 = g_regex_replace_literal(regex, buffer3, length, 0,
				calibrate->label[i], 0, NULL);
			g_free(buffer3);
		}
		g_regex_unref(regex);
		length = strlen(buffer2);
		snprintf(buffer, 32, "@value%u@", i + 1);
		regex = g_regex_new(buffer, 0, 0, NULL);
		snprintf(value, 32, calibrate->format[i],
		calibrate->value[simulation * calibrate->nvariables + i]);

#if DEBUG
printf("calibrate_parse: value=%s\n", value);
#endif
		buffer3 = g_regex_replace_literal(regex, buffer2, length, 0, value,
			0, NULL);
		g_free(buffer2);
		g_regex_unref(regex);
	}

	// Saving input file
	fwrite(buffer3, strlen(buffer3), sizeof(char), file);
	g_free(buffer3);
	fclose(file);

#if DEBUG
printf("calibrate_template: end\n");
#endif
}

/**
 * \fn double calibrate_parse(Calibrate *calibrate, unsigned int simulation, \
 *   unsigned int experiment)
 * \brief Function to parse input files, simulating and calculating the \
 *   objective function.
 * \param calibrate
 * \brief Calibration data.
 * \param simulation
 * \brief Simulation number.
 * \param experiment
 * \brief Experiment number.
 * \return Objective function value.
 */
double calibrate_parse(Calibrate *calibrate, unsigned int simulation,
	unsigned int experiment)
{
	unsigned int i;
	double e;
	char buffer[512], input[4][32], output[32], result[32];
	FILE *file_result;
	GMappedFile *file[4];

#if DEBUG
printf("calibrate_parse: start\n");
printf("calibrate_parse: simulation=%u experiment=%u\n", simulation,
experiment);
#endif

	file[0] = calibrate->file1[experiment];
	if (calibrate->ninputs > 1)
	{
		file[1] = calibrate->file2[experiment];
		if (calibrate->ninputs > 2)
		{
			file[2] = calibrate->file3[experiment];
			if (calibrate->ninputs > 3)
				file[3] = calibrate->file4[experiment];
		}
	}

	// Opening input files
	for (i = 0; i < calibrate->ninputs; ++i)
	{
		snprintf(&input[i][0], 32, "input-%u-%u-%u", i, simulation, experiment);
#if DEBUG
printf("calibrate_parse: i=%u input=%s\n", i, &input[i][0]);
#endif
		calibrate_template(calibrate, simulation, &input[i][0], file[i]);
	}
	for (; i < 4; ++i) snprintf(&input[i][0], 32, "");
#if DEBUG
printf("calibrate_parse: parsing end\n");
#endif

	// Performing the simulation
	snprintf(output, 32, "output-%u-%u", simulation, experiment);
	snprintf(result, 32, "result-%u-%u", simulation, experiment);
	snprintf(buffer, 512, "./%s %s %s %s %s %s", calibrate->simulator,
		&input[0][0], &input[1][0], &input[2][0], &input[3][0], output);
#if DEBUG
printf("calibrate_parse: %s\n", buffer);
#endif
	system(buffer);

	// Checking the objective value function
	snprintf(buffer, 512, "./%s %s %s %s", calibrate->evaluator, output,
		calibrate->experiment[experiment], result);
#if DEBUG
printf("calibrate_parse: %s\n", buffer);
#endif
	system(buffer);
	file_result = fopen(result, "r");
	e = atof(fgets(buffer, 512, file_result));
	fclose(file_result);

	// Removing files
#if !DEBUG
	snprintf(buffer, 512, "rm %s %s %s %s %s %s", &input[0][0], &input[1][0],
		&input[2][0], &input[3][0], output, result);
	system(buffer);
#endif

#if DEBUG
printf("calibrate_parse: end\n");
#endif

	// Returning the objective function
	return e;
}

/**
 * \fn void* calibrate_thread(ParallelData *data)
 * \brief Function to calibrate on a thread.
 * \param data
 * \brief Function data.
 * \return NULL
 */
void* calibrate_thread(ParallelData *data)
{
	unsigned int i, j, thread;
	double e;
	Calibrate *calibrate;
#if DEBUG
printf("calibrate_thread: start\n");
#endif
	thread = data->thread;
	calibrate = data->calibrate;
#if DEBUG
printf("calibrate_thread: thread=%u start=%u end=%u\n", thread,
calibrate->thread[thread], calibrate->thread[thread + 1]);
#endif
	for (i = calibrate->thread[thread]; i < calibrate->thread[thread + 1]; ++i)
	{
		e = 0.;
		for (j = 0; j < calibrate->nexperiments; ++j)
			e += calibrate_parse(calibrate, i, j);
		if (e < calibrate->error_best)
		{
			g_mutex_lock(&mutex);
			calibrate->error_best = e;
			calibrate->simulation_best = i;
			g_mutex_unlock(&mutex);
		}
#if DEBUG
printf("calibrate_thread: i=%u e=%lg\n", i, e);
#endif
	}
#if DEBUG
printf("calibrate_thread: end\n");
#endif
	g_thread_exit(NULL);
	return NULL;
}

/**
 * \fn void calibrate_sequential(Calibrate *calibrate)
 * \brief Function to calibrate sequentially.
 * \param calibrate
 * \brief Calibration data pointer.
 */
void calibrate_sequential(Calibrate *calibrate)
{
	unsigned int i, j;
	double e;
#if DEBUG
printf("calibrate_sequential: start\n");
#endif
	for (i = 0; i < calibrate->nsimulations; ++i)
	{
		e = 0.;
		for (j = 0; j < calibrate->nexperiments; ++j)
			e += calibrate_parse(calibrate, i, j);
		if (e < calibrate->error_best)
		{
			calibrate->error_best = e;
			calibrate->simulation_best = i;
		}
#if DEBUG
printf("calibrate_sequential: i=%u e=%lg\n", i, e);
#endif
	}
#if DEBUG
printf("calibrate_sequential: end\n");
#endif
}

/**
 * \fn void calibrate_sweep(Calibrate *calibrate)
 * \brief Function to calibrate with the sweep algorithm.
 * \param calibrate
 * \brief Calibration data pointer.
 */
void calibrate_sweep(Calibrate *calibrate)
{
	unsigned int i, j, k, l;
	double e;
	GThread *thread[calibrate->nthreads];
	ParallelData data[calibrate->nthreads];
#if DEBUG
printf("calibrate_sweep: start\n");
#endif
	for (i = 0; i < calibrate->nsimulations; ++i)
	{
		k = i;
		for (j = 0; j < calibrate->nvariables; ++j)
		{
			l = k % calibrate->nsweeps[j];
			k /= calibrate->nsweeps[j];
			e = calibrate->rangemin[j];
			if (calibrate->nsweeps[j] > 1)
				e += l * (calibrate->rangemax[j] - calibrate->rangemin[j])
					/ (calibrate->nsweeps[j] - 1);
			calibrate->value[i * calibrate->nvariables + j] = e;
		}
	}
	if (calibrate->nthreads <= 1)
		calibrate_sequential(calibrate);
	else
	{
		for (i = 0; i < calibrate->nthreads; ++i)
		{
			data[i].calibrate = calibrate;
			data[i].thread = i;
			thread[i] = g_thread_new(NULL, (void(*))calibrate_thread, &data[i]);
		}
		for (i = 0; i < calibrate->nthreads; ++i) g_thread_join(thread[i]);
	}
#if DEBUG
printf("calibrate_sweep: end\n");
#endif
}

/**
 * \fn void calibrate_MonteCarlo(Calibrate *calibrate)
 * \brief Function to calibrate with the Monte-Carlo algorithm.
 * \param calibrate
 * \brief Calibration data pointer.
 */
void calibrate_MonteCarlo(Calibrate *calibrate)
{
	unsigned int i, j;
	GThread *thread[calibrate->nthreads];
	ParallelData data[calibrate->nthreads];
#if DEBUG
printf("calibrate_MonteCarlo: start\n");
#endif
	for (i = 0; i < calibrate->nsimulations; ++i)
		for (j = 0; j < calibrate->nvariables; ++j)
			calibrate->value[i * calibrate->nvariables + j] =
				calibrate->rangemin[j] + gsl_rng_uniform(calibrate->rng)
				* (calibrate->rangemax[j] - calibrate->rangemin[j]);
	if (calibrate->nthreads <= 1)
		calibrate_sequential(calibrate);
	else
	{
		for (i = 0; i < calibrate->nthreads; ++i)
		{
			data[i].calibrate = calibrate;
			data[i].thread = i;
			thread[i] = g_thread_new(NULL, (void(*))calibrate_thread, &data[i]);
		}
		for (i = 0; i < calibrate->nthreads; ++i) g_thread_join(thread[i]);
	}
#if DEBUG
printf("calibrate_MonteCarlo: end\n");
#endif
}

/**
 * \fn void calibrate_genetic(Calibrate *calibrate)
 * \brief Function to calibrate with the Monte-Carlo algorithm.
 * \param calibrate
 * \brief Calibration data pointer.
 */
void calibrate_genetic(Calibrate *calibrate)
{
}

/**
 * \fn int calibrate_new(Calibrate *calibrate, char *filename)
 * \brief Function to open and perform a calibration.
 * \param calibrate
 * \brief Calibration data pointer.
 * \param filename
 * \brief Input data file name.
 * \return 1 on success, 0 on error.
 */
int calibrate_new(Calibrate *calibrate, char *filename)
{
	unsigned int i;
	char buffer2[512];
	xmlChar *buffer;
	xmlNode *node, *child;
	xmlDoc *doc;
#if HAVE_MPI
	double e;
	MPI_Status mpi_stat;
#endif

#if DEBUG
printf("calibrate_new: start\n");
#endif

	// Parsing the XML data file
	doc = xmlParseFile(filename);
	if (!doc)
	{
		printf("Unable to parse the data file %s\n", filename);
		return 0;
	}

	// Obtaining the root XML node
	node = xmlDocGetRootElement(doc);
	if (!node)
	{
		printf("No XML nodes in the data file\n");
		return 0;
	}
	if (xmlStrcmp(node->name, XML_CALIBRATE))
	{
		printf("Bad name of the XML root node in the data file\n");
		return 0;
	}

	// Obtaining the simulator file
	if (xmlHasProp(node, XML_SIMULATOR))
	{
		calibrate->simulator = (char*)xmlGetProp(node, XML_SIMULATOR);
	}
	else
	{
		printf("No simulator in the data file\n");
		return 0;
	}

	// Obtaining the evaluator file
	if (xmlHasProp(node, XML_EVALUATOR))
	{
		calibrate->evaluator = (char*)xmlGetProp(node, XML_EVALUATOR);
	}
	else
	{
		printf("No error in the data file\n");
		return 0;
	}

	// Reading the algorithm
	if (xmlHasProp(node, XML_ALGORITHM))
	{
		buffer = xmlGetProp(node, XML_ALGORITHM);
		if (!xmlStrcmp(buffer, XML_SWEEP))
		{
			calibrate->algorithm = CALIBRATE_ALGORITHM_SWEEP;
		}
		else
		{
			calibrate->algorithm = CALIBRATE_ALGORITHM_GENETIC;
		}
		xmlFree(buffer);
	}
	else
	{
		calibrate->algorithm = CALIBRATE_ALGORITHM_MONTE_CARLO;

		// Obtaining the simulations number
		if (xmlHasProp(node, XML_SIMULATIONS))
		{
			buffer = xmlGetProp(node, XML_SIMULATIONS);
			calibrate->nsimulations = strtoul((char*)buffer, NULL, 0);
			xmlFree(buffer);
		}
		else
		{
			printf("No simulations number in the data file\n");
			return 0;
		}
	}

	// Reading the experimental data file names
	calibrate->nexperiments = 0;
	calibrate->experiment = NULL;
	calibrate->template1 = calibrate->template2 = calibrate->template3
		= calibrate->template4 = NULL;
	calibrate->file1 = calibrate->file2 = calibrate->file3
		= calibrate->file4 = NULL;
	for (child = node->children; child; child = child->next)
	{
		if (xmlStrcmp(child->name, XML_EXPERIMENT)) break;
#if DEBUG
printf("calibrate_new: nexperiments=%u\n", calibrate->nexperiments);
#endif
		if (xmlHasProp(child, XML_NAME))
		{
			calibrate->experiment = realloc(calibrate->experiment,
				(1 + calibrate->nexperiments) * sizeof(char*));
			calibrate->experiment[calibrate->nexperiments] =
				(char*)xmlGetProp(child, XML_NAME);
		}
		else
		{
			printf("No experiment %u file name\n", calibrate->nexperiments + 1);
			return 0;
		}
		if (!calibrate->nexperiments) calibrate->ninputs = 0;
#if DEBUG
printf("calibrate_new: template1\n");
#endif
		if (xmlHasProp(child, XML_TEMPLATE1))
		{
			calibrate->template1 = realloc(calibrate->template1,
				(1 + calibrate->nexperiments) * sizeof(char*));
			calibrate->template1[calibrate->nexperiments] =
				(char*)xmlGetProp(child, XML_TEMPLATE1);
			calibrate->file1 = realloc(calibrate->file1,
				(1 + calibrate->nexperiments) * sizeof(GMappedFile*));
#if DEBUG
printf("calibrate_new: experiment=%u template1=%s\n", calibrate->nexperiments,
calibrate->template1[calibrate->nexperiments]);
#endif
			calibrate->file1[calibrate->nexperiments] =
				g_mapped_file_new
					(calibrate->template1[calibrate->nexperiments], 0, NULL);
			if (!calibrate->nexperiments) ++calibrate->ninputs;
#if DEBUG
printf("calibrate_new: ninputs=%u\n", calibrate->ninputs);
#endif
		}
		else
		{
			printf("No experiment %u template1\n", calibrate->nexperiments + 1);
			return 0;
		}
#if DEBUG
printf("calibrate_new: template2\n");
#endif
		if (xmlHasProp(child, XML_TEMPLATE2))
		{
			if (calibrate->nexperiments && calibrate->ninputs < 2)
			{
				printf("Experiment %u: bad templates number\n",
					calibrate->nexperiments + 1);
				return 0;
			}
			calibrate->template2 = realloc(calibrate->template2,
				(1 + calibrate->nexperiments) * sizeof(char*));
			calibrate->template2[calibrate->nexperiments] =
				(char*)xmlGetProp(child, XML_TEMPLATE2);
			calibrate->file2 = realloc(calibrate->file2,
				(1 + calibrate->nexperiments) * sizeof(GMappedFile*));
#if DEBUG
printf("calibrate_new: experiment=%u template2=%s\n", calibrate->nexperiments,
calibrate->template2[calibrate->nexperiments]);
#endif
			calibrate->file2[calibrate->nexperiments] =
				g_mapped_file_new
					(calibrate->template2[calibrate->nexperiments], 0, NULL);
			if (!calibrate->nexperiments) ++calibrate->ninputs;
#if DEBUG
printf("calibrate_new: ninputs=%u\n", calibrate->ninputs);
#endif
		}
		else if (calibrate->nexperiments && calibrate->ninputs > 1)
		{
			printf("No experiment %u template2\n", calibrate->nexperiments + 1);
			return 0;
		}
#if DEBUG
printf("calibrate_new: template3\n");
#endif
		if (xmlHasProp(child, XML_TEMPLATE3))
		{
			if (calibrate->nexperiments && calibrate->ninputs < 3)
			{
				printf("Experiment %u: bad templates number\n",
					calibrate->nexperiments + 1);
				return 0;
			}
			calibrate->template3 = realloc(calibrate->template3,
				(1 + calibrate->nexperiments) * sizeof(char*));
			calibrate->template3[calibrate->nexperiments] =
				(char*)xmlGetProp(child, XML_TEMPLATE3);
			calibrate->file3 = realloc(calibrate->file3,
				(1 + calibrate->nexperiments) * sizeof(GMappedFile*));
#if DEBUG
printf("calibrate_new: experiment=%u template3=%s\n", calibrate->nexperiments,
calibrate->template3[calibrate->nexperiments]);
#endif
			calibrate->file3[calibrate->nexperiments] =
				g_mapped_file_new
					(calibrate->template3[calibrate->nexperiments], 0, NULL);
			if (!calibrate->nexperiments) ++calibrate->ninputs;
#if DEBUG
printf("calibrate_new: ninputs=%u\n", calibrate->ninputs);
#endif
		}
		else if (calibrate->nexperiments && calibrate->ninputs > 2)
		{
			printf("No experiment %u template3\n", calibrate->nexperiments + 1);
			return 0;
		}
#if DEBUG
printf("calibrate_new: template3\n");
#endif
		if (xmlHasProp(child, XML_TEMPLATE4))
		{
			if (calibrate->nexperiments && calibrate->ninputs < 4)
			{
				printf("Experiment %u: bad templates number\n",
					calibrate->nexperiments + 1);
				return 0;
			}
			calibrate->template4 = realloc(calibrate->template4,
				(1 + calibrate->nexperiments) * sizeof(char*));
			calibrate->template4[calibrate->nexperiments] =
				(char*)xmlGetProp(child, XML_TEMPLATE4);
			calibrate->file4 = realloc(calibrate->file4,
				(1 + calibrate->nexperiments) * sizeof(GMappedFile*));
#if DEBUG
printf("calibrate_new: experiment=%u template4=%s\n", calibrate->nexperiments,
calibrate->template4[calibrate->nexperiments]);
#endif
			calibrate->file4[calibrate->nexperiments] =
				g_mapped_file_new
					(calibrate->template4[calibrate->nexperiments], 0, NULL);
			if (!calibrate->nexperiments) ++calibrate->ninputs;
#if DEBUG
printf("calibrate_new: ninputs=%u\n", calibrate->ninputs);
#endif
		}
		else if (calibrate->nexperiments && calibrate->ninputs > 3)
		{
			printf("No experiment %u template4\n", calibrate->nexperiments + 1);
			return 0;
		}
		++calibrate->nexperiments;
#if DEBUG
printf("calibrate_new: nexperiments=%u\n", calibrate->nexperiments);
#endif
	}
	if (!calibrate->nexperiments)
	{
		printf("No calibration experiments\n");
		return 0;
	}

	// Reading the variables data
	calibrate->nvariables = 0;
	calibrate->label = NULL;
	calibrate->rangemin = NULL;
	calibrate->rangemax = NULL;
	calibrate->format = NULL;
	calibrate->nsweeps = NULL;
	if (calibrate->algorithm == CALIBRATE_ALGORITHM_SWEEP)
		calibrate->nsimulations = 1;
	for (; child; child = child->next)
	{
		if (xmlStrcmp(child->name, XML_VARIABLE))
		{
			printf("Bad XML node\n");
			return 0;
		}
		if (xmlHasProp(child, XML_NAME))
		{
			calibrate->label = realloc(calibrate->label,
				(1 + calibrate->nvariables) * sizeof(char*));
			calibrate->label[calibrate->nvariables] =
				(char*)xmlGetProp(child, XML_NAME);
		}
		else
		{
			printf("No variable %u name\n", calibrate->nvariables + 1);
			return 0;
		}
		if (xmlHasProp(child, XML_MINIMUM))
		{
			calibrate->rangemin = realloc(calibrate->rangemin,
				(1 + calibrate->nvariables) * sizeof(double));
			buffer = xmlGetProp(child, XML_MINIMUM);
			calibrate->rangemin[calibrate->nvariables] = atof((char*)buffer);
			xmlFree(buffer);
		}
		else
		{
			printf("No variable %u minimum range\n", calibrate->nvariables + 1);
			return 0;
		}
		if (xmlHasProp(child, XML_MAXIMUM))
		{
			calibrate->rangemax = realloc(calibrate->rangemax,
				(1 + calibrate->nvariables) * sizeof(double));
			buffer = xmlGetProp(child, XML_MAXIMUM);
			calibrate->rangemax[calibrate->nvariables] = atof((char*)buffer);
			xmlFree(buffer);
		}
		else
		{
			printf("No variable %u maximum range\n", calibrate->nvariables + 1);
			return 0;
		}
		calibrate->format = realloc(calibrate->format,
			(1 + calibrate->nvariables) * sizeof(char*));
		if (xmlHasProp(child, XML_FORMAT))
		{
			calibrate->format[calibrate->nvariables] =
				(char*)xmlGetProp(child, XML_FORMAT);
		}
		else
		{
			calibrate->format[calibrate->nvariables] =
				(char*)xmlStrdup(DEFAULT_FORMAT);
		}
		if (calibrate->algorithm == CALIBRATE_ALGORITHM_SWEEP)
		{
			if (xmlHasProp(child, XML_SWEEPS))
			{
				calibrate->nsweeps = realloc(calibrate->nsweeps,
					(1 + calibrate->nvariables) * sizeof(unsigned int));
				buffer = xmlGetProp(child, XML_SWEEPS);
				calibrate->nsweeps[calibrate->nvariables] =
					strtoul((char*)buffer, NULL, 0);
				xmlFree(buffer);
			}
			else
			{
				printf("No variable %u sweeps number\n",
					calibrate->nvariables + 1);
				return 0;
			}
			calibrate->nsimulations *=
				calibrate->nsweeps[calibrate->nvariables];
#if DEBUG
printf("calibrate_new: nsweeps=%u nsimulations=%u\n",
calibrate->nsweeps[calibrate->nvariables], calibrate->nsimulations);
#endif
		}
		++calibrate->nvariables;
	}
	if (!calibrate->nvariables)
	{
		printf("No calibration variables\n");
		return 0;
	}
#if DEBUG
printf("calibrate_new: nvariables=%u\n", calibrate->nvariables);
#endif

	// Allocating values
	calibrate->value = (double*)malloc(calibrate->nsimulations *
		calibrate->nvariables * sizeof(double));

	// Calculating simulations to perform on each task
#ifdef HAVE_MPI
	calibrate->nstart = calibrate->mpi_rank * calibrate->nsimulations
		/ calibrate->mpi_tasks;
	calibrate->nend = (1 + calibrate->mpi_rank) * calibrate->nsimulations
		/ calibrate->mpi_tasks;
#else
	calibrate->nstart = 0;
	calibrate->nend = calibrate->nsimulations;
#endif
#if DEBUG
printf("calibrate_new: nstart=%u nend=%u\n", calibrate->nstart,
calibrate->nend);
#endif

	// Calculating simulations to perform on each thread
	calibrate->thread =
		(unsigned int*)malloc((1 + calibrate->nthreads) * sizeof(unsigned int));
	for (i = 0; i <= calibrate->nthreads; ++i)
	   calibrate->thread[i] = calibrate->nstart
		   + i * (calibrate->nend - calibrate->nstart) / calibrate->nthreads;

	// Performing the algorithm
	calibrate->error_best = INFINITY;
	switch (calibrate->algorithm)
	{
		// Sweep algorithm
		case CALIBRATE_ALGORITHM_SWEEP:
			calibrate_sweep(calibrate);
			break;

		// Genetic algorithm
		case CALIBRATE_ALGORITHM_GENETIC:
			calibrate_genetic(calibrate);
			break;

		// Default Monte-Carlo algorithm
		default:
			calibrate_MonteCarlo(calibrate);
	}

#ifdef HAVE_MPI
	// Communicating tasks results
	if (calibrate->mpi_rank == 0)
	{
		for (i = 1; i < calibrate->mpi_tasks; ++i)
		{
			MPI_Recv(&e, 1, MPI_DOUBLE, i, 1, MPI_COMM_WORLD, &mpi_stat);
			MPI_Recv(&j, 1, MPI_INT, i, 1, MPI_COMM_WORLD, &mpi_stat);
			if (e < calibrate->error_best)
			{
				calibrate->error_best = e;
				calibrate->simulation_best = j;
			}
		}
	}
	else
	{
		MPI_Send(&calibrate->error_best, 1, MPI_DOUBLE, 0, 1, MPI_COMM_WORLD);
		MPI_Send(&calibrate->simulation_best, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);
	}
#endif

	// Closing the XML document
	xmlFreeDoc(doc);

	// Best choices
#if HAVE_MPI
	if (!calibrate->mpi_rank)
	{
#endif
	printf("THE BEST IS\n");
	printf("error=%le\n", calibrate->error_best);
	for (i = 0; i < calibrate->nvariables; ++i)
	{
		snprintf(buffer2, 512, "parameter%%u=%s\n", calibrate->format[i]);
		printf(buffer2, i, calibrate->value[calibrate->simulation_best
			* calibrate->nvariables + i]);
	}
#if HAVE_MPI
	}
#endif

	// Freeing memory
	xmlFree(calibrate->simulator);
	xmlFree(calibrate->evaluator);
	for (i = 0; i < calibrate->nexperiments; ++i)
	{
		xmlFree(calibrate->experiment[i]);
		xmlFree(calibrate->template1[i]);
		g_mapped_file_unref(calibrate->file1[i]);
		if (calibrate->ninputs > 1)
		{
			xmlFree(calibrate->template2[i]);
			g_mapped_file_unref(calibrate->file2[i]);
			if (calibrate->ninputs > 2)
			{
				xmlFree(calibrate->template3[i]);
				g_mapped_file_unref(calibrate->file3[i]);
				if (calibrate->ninputs > 3)
				{
					xmlFree(calibrate->template4[i]);
					g_mapped_file_unref(calibrate->file4[i]);
				}
			}
		}
	}
	free(calibrate->experiment);
	free(calibrate->template1);
	free(calibrate->file1);
	if (calibrate->ninputs > 1)
	{
		free(calibrate->template2);
		free(calibrate->file2);
		if (calibrate->ninputs > 2)
		{
			free(calibrate->template3);
			free(calibrate->file3);
			if (calibrate->ninputs > 3)
			{
				free(calibrate->template4);
				free(calibrate->file4);
			}
		}
	}
	for (i = 0; i < calibrate->nvariables; ++i)
	{
		xmlFree(calibrate->label[i]);
		xmlFree(calibrate->format[i]);
	}
	free(calibrate->label);
	free(calibrate->rangemin);
	free(calibrate->rangemax);
	free(calibrate->format);
	free(calibrate->nsweeps);
	free(calibrate->value);
	free(calibrate->thread);

#if DEBUG
printf("calibrate_new: end\n");
#endif

	return 1;
}

/**
 * \fn int cores_number()
 * \brief Function to obtain the cores number.
 * \return Cores number.
 */
int cores_number()
{
#ifdef G_OS_WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#else
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

/**
 * \fn int main(int argn, char **argc)
 * \brief Main function.
 * \param argn
 * \brief Arguments number.
 * \param argc
 * \brief Arguments pointer.
 * \return 0 on success, >0 on error.
 */
int main(int argn, char **argc)
{
	Calibrate calibrate[1];

#ifdef HAVE_MPI
	// Starting MPI
	MPI_Init(&argn, &argc);
	MPI_Comm_size(MPI_COMM_WORLD, &calibrate->mpi_tasks);
	MPI_Comm_rank(MPI_COMM_WORLD, &calibrate->mpi_rank);
	printf("rank=%d tasks=%d\n", calibrate->mpi_rank, calibrate->mpi_tasks);
#endif

	// Checking sintaxis
	if (!(argn == 2 || (argn == 4 && !strcmp(argc[1], "-nthreads"))))
	{
		printf("The sintaxis is:\ncalibrator [-nthreads x] data_file\n");
#ifdef HAVE_MPI
		// Closing MPI
		MPI_Finalize();
#endif
		return 1;
	}

	// Starting GThreads
	if (argn == 2) calibrate->nthreads = cores_number();
	else calibrate->nthreads = atoi(argc[2]);
	printf("nthreads=%u\n", calibrate->nthreads);

	// Starting pseudo-random numbers generator
	calibrate->rng = gsl_rng_alloc(gsl_rng_taus2);
	gsl_rng_set(calibrate->rng, RANDOM_SEED);

	// Allowing spaces in the XML data file
	xmlKeepBlanksDefault(0);

	// Making calibration
	calibrate_new(calibrate, argc[argn - 1]);

	// Freeing memory
	gsl_rng_free(calibrate->rng);

#ifdef HAVE_MPI
	// Closing MPI
	MPI_Finalize();
#endif

	return 0;
}
