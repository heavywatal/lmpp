data {
    real<lower=0,upper=1> threshold;
    real<lower=0,upper=1> intercept;
    real<lower=0> sd_coef;
    int n_samples;
    int n_coefs;
    matrix[n_samples, n_coefs] genotypes;
}

parameters {
    simplex[n_coefs] coefs;
}

model {
    vector[n_samples] mu;
    vector[n_samples] sigma;
    mu = genotypes * coefs * (1.0 - intercept);
    for (i in 1:n_samples) {
        sigma[i] = sqrt(mu[i] + intercept);
    }
    target += normal_lccdf(threshold | mu + intercept, sigma * sd_coef);
}