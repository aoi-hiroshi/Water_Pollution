-- Select WATER_DB_NAME before running. This creates absent tables only;
-- it does NOT alter existing schemas, replace data or create application users.
CREATE TABLE IF NOT EXISTS company_info (
    company_id BIGINT NOT NULL PRIMARY KEY,
    company_name VARCHAR(128) NOT NULL,
    company_code VARCHAR(64) NOT NULL,
    task_type VARCHAR(64) NULL,
    location VARCHAR(255) NULL,
    description TEXT NULL,
    created_at DATETIME NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS train_data (
    id BIGINT NOT NULL PRIMARY KEY,
    temperature DOUBLE NULL, ph DOUBLE NULL, cod DOUBLE NULL,
    nh3n DOUBLE NULL, tp DOUBLE NULL, water_level DOUBLE NULL, orp DOUBLE NULL,
    conductivity DOUBLE NULL, dissolved_oxygen DOUBLE NULL, turbidity DOUBLE NULL,
    company_id BIGINT NOT NULL,
    INDEX idx_train_company_sample(company_id,id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS test_data LIKE train_data;
