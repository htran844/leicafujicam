/* AGC 9.6.19 V7 noise model 26: GC02M1 - LMIPRO.
 * Extracted from libagc.so, SHA-256:
 * b41bf1ab8c775e6afca35aab159935e83be01dff6bffd246d77d7920e5094262
 * A/B/C/D at ELF VA 0x3b76e0/0x3b7700/0x3b7720/0x3b7740.
 * Max analog ISO: 600 (ELF VA 0x3b6568).
 * Standard GCam .c profile: planes R, Gr, Gb, B; input is the actual ISO.
 * No AGC recipe ISO remapping or denoise-strength multipliers are included.
 */

double compute_noise_model_entry_S(int plane, int sens) {
    static double noise_model_A[] = { 0.0000072370178460895120, 0.0000065231882496758227, 0.0000068691380416554428, 0.0000079136191584250803 };
    static double noise_model_B[] = { -0.00011073147252834658, -0.000048916872447121773, -0.000075754391181991293, -0.00012886838607408598 };
    double A = noise_model_A[plane];
    double B = noise_model_B[plane];
    double s = A * sens + B;
    return s < 0.0 ? 0.0 : s;
}

double compute_noise_model_entry_O(int plane, int sens) {
    static double noise_model_C[] = { 4.3911268460890447e-11, 6.5100383410561773e-11, 8.2524555196144753e-11, 4.9758523419218131e-11 };
    static double noise_model_D[] = { 0.0000025261928220997869, 0.0000022153976695516784, 0.0000027291459915261260, 0.0000026378960491808950 };
    double digital_gain = (sens / 600.0) < 1.0 ? 1.0 : (sens / 600.0);
    double C = noise_model_C[plane];
    double D = noise_model_D[plane];
    double o = C * sens * sens + D * digital_gain * digital_gain;
    return o < 0.0 ? 0.0 : o;
}
