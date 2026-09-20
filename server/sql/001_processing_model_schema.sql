-- Fresh-install step 2/3. Run after 000_base_schema.sql.
-- The tables below hold reproducible processing/model history. Raw samples in
-- water_samples are never overwritten.

CREATE TABLE IF NOT EXISTS processing_runs (
    run_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    task_type VARCHAR(32) NOT NULL,
    company_id BIGINT NOT NULL,
    data_split ENUM('train','validation','test') NOT NULL,
    operation VARCHAR(32) NOT NULL,
    algorithm_name VARCHAR(96) NOT NULL,
    algorithm_version VARCHAR(32) NULL,
    parameters_json JSON NULL,
    parent_run_id BIGINT UNSIGNED NULL,
    window_size INT UNSIGNED NULL,
    z_threshold DOUBLE NULL,
    sample_count BIGINT UNSIGNED NOT NULL,
    changed_count BIGINT UNSIGNED NOT NULL DEFAULT 0,
    status VARCHAR(16) NOT NULL DEFAULT 'completed',
    started_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at TIMESTAMP NULL,
    CONSTRAINT fk_processing_company FOREIGN KEY(company_id)
        REFERENCES company_info(company_id),
    CONSTRAINT fk_processing_parent FOREIGN KEY(parent_run_id)
        REFERENCES processing_runs(run_id),
    CONSTRAINT chk_processing_task
        CHECK(task_type IN ('classification','forecast')),
    CONSTRAINT chk_processing_status
        CHECK(status IN ('pending','running','completed','failed')),
    UNIQUE KEY uk_processing_run_company(run_id,company_id),
    INDEX idx_processing_scope(task_type,company_id,data_split,started_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS processed_samples (
    run_id BIGINT UNSIGNED NOT NULL,
    source_sample_id BIGINT UNSIGNED NOT NULL,
    company_id BIGINT NOT NULL,
    temperature DOUBLE NULL,
    ph DOUBLE NULL,
    cod DOUBLE NULL,
    nh3n DOUBLE NULL,
    tp DOUBLE NULL,
    water_level DOUBLE NULL,
    orp DOUBLE NULL,
    conductivity DOUBLE NULL,
    dissolved_oxygen DOUBLE NULL,
    turbidity DOUBLE NULL,
    changed_mask INT UNSIGNED NOT NULL DEFAULT 0,
    quality_flags JSON NULL,
    PRIMARY KEY(run_id,source_sample_id),
    CONSTRAINT fk_processed_run_company FOREIGN KEY(run_id,company_id)
        REFERENCES processing_runs(run_id,company_id),
    CONSTRAINT fk_processed_source_company
        FOREIGN KEY(source_sample_id,company_id)
        REFERENCES water_samples(sample_id,company_id),
    INDEX idx_processed_source(source_sample_id,run_id),
    INDEX idx_processed_company(company_id,run_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS model_registry (
    model_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    task_type VARCHAR(32) NOT NULL,
    model_name VARCHAR(96) NOT NULL,
    model_version VARCHAR(64) NOT NULL,
    framework VARCHAR(32) NOT NULL DEFAULT 'onnxruntime',
    artifact_path VARCHAR(512) NOT NULL,
    artifact_sha256 CHAR(64) NULL,
    input_contract_json JSON NOT NULL,
    output_contract_json JSON NOT NULL,
    feature_order_json JSON NOT NULL,
    preprocessing_json JSON NULL,
    training_data_hash CHAR(64) NULL,
    status VARCHAR(16) NOT NULL DEFAULT 'inactive',
    trained_at DATETIME NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT chk_model_task
        CHECK(task_type IN ('classification','forecast')),
    CONSTRAINT chk_model_status
        CHECK(status IN ('inactive','active','retired','failed')),
    UNIQUE KEY uk_model_version(task_type,model_name,model_version),
    INDEX idx_model_active(task_type,status,created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS model_metrics (
    metric_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    model_id BIGINT UNSIGNED NOT NULL,
    data_split ENUM('train','validation','test') NOT NULL,
    target_name VARCHAR(64) NOT NULL DEFAULT 'overall',
    horizon_step INT UNSIGNED NOT NULL DEFAULT 0,
    metric_name VARCHAR(64) NOT NULL,
    metric_value DOUBLE NOT NULL,
    details_json JSON NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_metric_model FOREIGN KEY(model_id)
        REFERENCES model_registry(model_id),
    UNIQUE KEY uk_model_metric(
        model_id,data_split,target_name,horizon_step,metric_name
    )
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS classification_results (
    result_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    source_sample_id BIGINT UNSIGNED NOT NULL,
    processing_run_id BIGINT UNSIGNED NULL,
    model_id BIGINT UNSIGNED NOT NULL,
    predicted_company_id BIGINT NOT NULL,
    confidence DOUBLE NULL,
    probabilities_json JSON NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_class_result_source FOREIGN KEY(source_sample_id)
        REFERENCES water_samples(sample_id),
    CONSTRAINT fk_class_result_processing FOREIGN KEY(processing_run_id)
        REFERENCES processing_runs(run_id),
    CONSTRAINT fk_class_result_model FOREIGN KEY(model_id)
        REFERENCES model_registry(model_id),
    CONSTRAINT fk_class_result_company FOREIGN KEY(predicted_company_id)
        REFERENCES company_info(company_id),
    CONSTRAINT chk_class_result_confidence
        CHECK(confidence IS NULL OR (confidence >= 0 AND confidence <= 1)),
    INDEX idx_class_result_lookup(source_sample_id,model_id,created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS forecast_runs (
    forecast_run_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    company_id BIGINT NOT NULL,
    input_end_sample_id BIGINT UNSIGNED NOT NULL,
    processing_run_id BIGINT UNSIGNED NULL,
    model_id BIGINT UNSIGNED NOT NULL,
    lookback_steps INT UNSIGNED NOT NULL,
    horizon_steps INT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_forecast_company FOREIGN KEY(company_id)
        REFERENCES company_info(company_id),
    CONSTRAINT fk_forecast_input_company
        FOREIGN KEY(input_end_sample_id,company_id)
        REFERENCES water_samples(sample_id,company_id),
    CONSTRAINT fk_forecast_processing_company
        FOREIGN KEY(processing_run_id,company_id)
        REFERENCES processing_runs(run_id,company_id),
    CONSTRAINT fk_forecast_model FOREIGN KEY(model_id)
        REFERENCES model_registry(model_id),
    INDEX idx_forecast_lookup(company_id,created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS forecast_points (
    forecast_run_id BIGINT UNSIGNED NOT NULL,
    horizon_step INT UNSIGNED NOT NULL,
    forecast_at DATETIME(6) NULL,
    cod DOUBLE NOT NULL,
    nh3n DOUBLE NOT NULL,
    tp DOUBLE NOT NULL,
    turbidity DOUBLE NOT NULL,
    PRIMARY KEY(forecast_run_id,horizon_step),
    CONSTRAINT fk_forecast_point_run FOREIGN KEY(forecast_run_id)
        REFERENCES forecast_runs(forecast_run_id) ON DELETE CASCADE,
    CONSTRAINT chk_forecast_horizon CHECK(horizon_step > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
