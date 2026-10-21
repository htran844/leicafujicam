/* Generated test code to dump a table of data for external validation
 * of the noise model parameters.
 */
#include <stdio.h>
#include <assert.h>
double compute_noise_model_entry_S(int plane, int sens);
double compute_noise_model_entry_O(int plane, int sens);
int main(void) {
    for (int plane = 0; plane < 4; plane++) {
        for (int sens = 50; sens <= 1600; sens += 100) {
            double o = compute_noise_model_entry_O(plane, sens);
            double s = compute_noise_model_entry_S(plane, sens);
            printf("%d,%d,%lf,%lf\n", plane, sens, o, s);
        }
    }
    return 0;
}

/* Generated functions to map a given sensitivity to the O and S noise
 * model parameters in the DNG noise model. The planes are in
 * R, Gr, Gb, B order.
 */
double compute_noise_model_entry_S(int plane, int sens) {
    static double noise_model_A[] = { 3.729645920998918e-06,3.782009250290596e-06,3.8085745800818437e-06,3.7262479743019606e-06 };
    static double noise_model_B[] = { 8.157411477824238e-06,9.084036440208794e-06,5.882073902516482e-06,9.997743561697092e-06 };
    double A = noise_model_A[plane];
    double B = noise_model_B[plane];
    double s = A * sens + B;
    return s < 0.0 ? 0.0 : s;
}

double compute_noise_model_entry_O(int plane, int sens) {
    static double noise_model_C[] = { 1.3156833672393117e-10,1.332407774827434e-10,1.3146960863459476e-10,1.3063345467612423e-10 };
    static double noise_model_D[] = { 1.2092197219330786e-06,1.1097618286944459e-06,1.1687899062887236e-06,1.1822263493014864e-06 };
    double digital_gain = (sens / 800.0) < 1.0 ? 1.0 : (sens / 800.0);
    double C = noise_model_C[plane];
    double D = noise_model_D[plane];
    double o = C * sens * sens + D * digital_gain * digital_gain;
    return o < 0.0 ? 0.0 : o;
}
