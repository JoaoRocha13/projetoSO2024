#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#define MAX_POINTS 1000000
#define BUFFER_SIZE 1024

typedef struct {
    double x;
    double y;
} Point;

typedef struct {
    Point *polygon;
    int polygon_size;
    int total_points;
    int points_inside;
    int points_checked;
    pthread_mutex_t mutex;
} SharedData;

/**
 * @brief Determines the orientation of an ordered triplet (p, q, r).
 * @param p First point of the triplet.
 * @param q Second point of the triplet.
 * @param r Third point of the triplet.
 * @return 0 if p, q, and r are colinear, 1 if clockwise, 2 if counterclockwise.
 */
int orientation(Point p, Point q, Point r) {
    double val = (q.y - p.y) * (r.x - q.x) - (q.x - p.x) * (r.y - q.y);

    if (val == 0) return 0;
    return (val > 0) ? 1 : 2;
}

/**
 * @brief Checks if point q lies on line segment pr.
 * @param p First point of the line segment.
 * @param q Point to check.
 * @param r Second point of the line segment.
 * @return true if point q lies on line segment pr, else false.
 */
bool onSegment(Point p, Point q, Point r) {
    if (q.x <= fmax(p.x, r.x) && q.x >= fmin(p.x, r.x) &&
        q.y <= fmax(p.y, r.y) && q.y >= fmin(p.y, r.y))
        return true;

    return false;
}

/**
 * @brief Checks if line segments p1q1 and p2q2 intersect.
 * @param p1 First point of the first line segment.
 * @param q1 Second point of the first line segment.
 * @param p2 First point of the second line segment.
 * @param q2 Second point of the second line segment.
 * @return true if line segments p1q1 and p2q2 intersect, else false.
 */
bool doIntersect(Point p1, Point q1, Point p2, Point q2) {
    int o1 = orientation(p1, q1, p2);
    int o2 = orientation(p1, q1, q2);
    int o3 = orientation(p2, q2, p1);
    int o4 = orientation(p2, q2, q1);

    if (o1 != o2 && o3 != o4)
        return true;

    if (o1 == 0 && onSegment(p1, p2, q1)) return true;
    if (o2 == 0 && onSegment(p1, q2, q1)) return true;
    if (o3 == 0 && onSegment(p2, p1, q2)) return true;
    if (o4 == 0 && onSegment(p2, q1, q2)) return true;

    return false;
}

/**
 * @brief Checks if a point p is inside a polygon of n points.
 * @param polygon[] Array of points forming the polygon.
 * @param n Number of points in the polygon.
 * @param p Point to check.
 * @return true if the point p is inside the polygon, else false.
 */
bool isInsidePolygon(Point polygon[], int n, Point p) {
    if (n < 3) return false;

    Point extreme = {2.5, p.y};

    int count = 0, i = 0;
    do {
        int next = (i + 1) % n;

        if (doIntersect(polygon[i], polygon[next], p, extreme)) {
            if (orientation(polygon[i], p, polygon[next]) == 0)
                return onSegment(polygon[i], p, polygon[next]);
            count++;
        }
        i = next;
    } while (i != 0);

    return count & 1;
}

void *worker_thread(void *arg) {
    SharedData *data = (SharedData *)arg;
    int points_per_thread = data->total_points;

    for (int i = 0; i < points_per_thread; i++) {
        Point p = {(double)rand() / RAND_MAX * 2, (double)rand() / RAND_MAX * 2};

        pthread_mutex_lock(&data->mutex);
        data->points_checked++;
        if (isInsidePolygon(data->polygon, data->polygon_size, p)) {
            data->points_inside++;
        }
        pthread_mutex_unlock(&data->mutex);
    }

    return NULL;
}

void *progress_bar(void *arg) {
    SharedData *data = (SharedData *)arg;

    while (true) {
        sleep(1);

        pthread_mutex_lock(&data->mutex);
        int checked = data->points_checked;
        pthread_mutex_unlock(&data->mutex);

        int progress = (checked * 100) / data->total_points;
        printf("\rProgresso: %d%%", progress);
        fflush(stdout);

        if (checked >= data->total_points) {
            break;
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <arquivo_do_poligono> <num_threads> <num_pontos_aleatorios>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *poligono = argv[1];
    int num_threads = atoi(argv[2]);
    int num_pontos_aleatorios = atoi(argv[3]);

    if (num_threads <= 0 || num_pontos_aleatorios <= 0) {
        fprintf(stderr, "Erro: Números de threads e pontos devem ser maiores que 0.\n");
        return EXIT_FAILURE;
    }

    int arquivo = open(poligono, O_RDONLY);
    if (arquivo < 0) {
        perror("Erro ao abrir o arquivo do polígono");
        return EXIT_FAILURE;
    }

    Point *polygon = malloc(MAX_POINTS * sizeof(Point));
    if (polygon == NULL) {
        perror("Erro ao alocar memória para o polígono");
        close(arquivo);
        return EXIT_FAILURE;
    }

    int capacity = MAX_POINTS;
    int n = 0;
    char buffer[128];
    ssize_t bytesRead;
    char *line, *saveptr;

    while ((bytesRead = read(arquivo, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytesRead] = '\0';
        line = strtok_r(buffer, "\n", &saveptr);
        while (line != NULL) {
            if (sscanf(line, "%lf %lf", &polygon[n].x, &polygon[n].y) == 2) {
                n++;
                if (n >= capacity) {
                    fprintf(stderr, "Erro: Número máximo de pontos do polígono excedido.\n");
                    close(arquivo);
                    return EXIT_FAILURE;
                }
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
    }

    close(arquivo);

    if (n < 3) {
        fprintf(stderr, "Polígono inválido ou dados insuficientes no arquivo.\n");
        free(polygon);
        return EXIT_FAILURE;
    }

    SharedData data;
    data.polygon = polygon;
    data.polygon_size = n;
    data.total_points = num_pontos_aleatorios;
    data.points_inside = 0;
    data.points_checked = 0;
    pthread_mutex_init(&data.mutex, NULL);

    pthread_t worker_threads[num_threads];
    pthread_t progress_thread;

    srand((unsigned int)time(NULL));

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&worker_threads[i], NULL, worker_thread, &data);
    }
    pthread_create(&progress_thread, NULL, progress_bar, &data);

    for (int i = 0; i < num_threads; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    pthread_join(progress_thread, NULL);

    double area_of_reference = 4.0;
    double estimated_area = ((double)data.points_inside / num_pontos_aleatorios) * area_of_reference;
    printf("\nÁrea estimada do polígono: %.2f unidades quadradas\n", estimated_area);

    free(polygon);
    pthread_mutex_destroy(&data.mutex);

    return EXIT_SUCCESS;
}
