# AWS S3 Setup for OTA Firmware Hosting

## Prerequisites

- AWS Account (free tier eligible: https://aws.amazon.com/free/)
- AWS CLI installed

## Installation

### Install AWS CLI

**Linux/macOS:**
```bash
pip install awscli
```

**Windows:**
Download from: https://aws.amazon.com/cli/

### Configure AWS Credentials

```bash
# Configure AWS CLI (interactive)
aws configure

# Enter:
# AWS Access Key ID: [Get from AWS IAM console]
# AWS Secret Access Key: [Get from AWS IAM console]
# Default region: us-east-1
# Default output format: json
```

**Get AWS Credentials:**
1. Go to AWS Console: https://console.aws.amazon.com/
2. Navigate to: IAM → Users → Your User → Security Credentials
3. Create Access Key → "Application running on AWS compute service"
4. Download and save credentials securely

## Create S3 Bucket for Firmware

```bash
# Create bucket (choose a globally unique name)
aws s3 mb s3://your-company-iot-firmware --region us-east-1

# Verify bucket created
aws s3 ls
```

**Important:** Bucket names must be globally unique across all AWS accounts.

## Set Bucket Policy (Public Read for Firmware)

Create a file `bucket-policy.json`:
```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Sid": "PublicReadFirmware",
      "Effect": "Allow",
      "Principal": "*",
      "Action": "s3:GetObject",
      "Resource": "arn:aws:s3:::your-company-iot-firmware/*"
    }
  ]
}
```

Apply policy:
```bash
aws s3api put-bucket-policy \
  --bucket your-company-iot-firmware \
  --policy file://bucket-policy.json
```

**Security Note:** This allows public read access to firmware files. ESP32 devices can download without authentication. For production, consider:
- Using signed URLs with expiration
- Restricting by IP range
- Using AWS IoT authentication

## Enable Public Access

AWS blocks public access by default. To allow firmware downloads:

```bash
aws s3api put-public-access-block \
  --bucket your-company-iot-firmware \
  --public-access-block-configuration \
  "BlockPublicAcls=false,IgnorePublicAcls=false,BlockPublicPolicy=false,RestrictPublicBuckets=false"
```

## Test Upload

```bash
# Upload test file
echo "test" > test.txt
aws s3 cp test.txt s3://your-company-iot-firmware/test.txt --acl public-read

# Get public URL
echo "https://your-company-iot-firmware.s3.us-east-1.amazonaws.com/test.txt"

# Test download (should work without authentication)
curl https://your-company-iot-firmware.s3.us-east-1.amazonaws.com/test.txt
```

## Configure Upload Script

Update `ESP32/tools/upload_firmware.py` or set environment variables:

**Option 1: Use AWS default credentials** (recommended)
```bash
# No environment variables needed!
# Script uses ~/.aws/credentials automatically
```

**Option 2: Explicit credentials**
```bash
export AWS_REGION="us-east-1"
export AWS_ACCESS_KEY_ID="your-access-key"
export AWS_SECRET_ACCESS_KEY="your-secret-key"
export S3_BUCKET="your-company-iot-firmware"
```

## Cost Monitoring

AWS Free Tier (first 12 months):
- **Storage**: 5 GB free
- **Transfers**: 15 GB/month free (outbound to internet)
- **Requests**: 20,000 GET requests/month free

After free tier:
- **Storage**: $0.023/GB/month
- **Transfers**: $0.09/GB (first 10 TB/month)
- **GET requests**: $0.0004 per 1,000 requests

**Example cost** (100 devices, 10 firmware versions):
- Storage: 20 MB = $0.0005/month
- Transfers: 200 MB/month = $0.018/month
- **Total: ~$0.02/month**

Set up billing alerts:
```bash
# Get notified if costs exceed $5/month
aws cloudwatch put-metric-alarm \
  --alarm-name billing-alert \
  --alarm-description "Alert if AWS costs exceed $5" \
  --metric-name EstimatedCharges \
  --namespace AWS/Billing \
  --statistic Maximum \
  --period 21600 \
  --evaluation-periods 1 \
  --threshold 5 \
  --comparison-operator GreaterThanThreshold
```

## Security Best Practices

### 1. Create IAM User for Uploads (Don't use root account)

```bash
# Create IAM user
aws iam create-user --user-name ota-uploader

# Attach S3 upload policy
aws iam attach-user-policy \
  --user-name ota-uploader \
  --policy-arn arn:aws:iam::aws:policy/AmazonS3FullAccess

# Create access key
aws iam create-access-key --user-name ota-uploader
```

### 2. Enable S3 Versioning (Rollback protection)

```bash
aws s3api put-bucket-versioning \
  --bucket your-company-iot-firmware \
  --versioning-configuration Status=Enabled
```

### 3. Enable Server-Side Encryption

```bash
aws s3api put-bucket-encryption \
  --bucket your-company-iot-firmware \
  --server-side-encryption-configuration '{
    "Rules": [{
      "ApplyServerSideEncryptionByDefault": {
        "SSEAlgorithm": "AES256"
      }
    }]
  }'
```

## Optional: Local Development with MinIO

For offline testing, you can use MinIO (S3-compatible) locally:

```bash
# Run MinIO in Docker
docker run -p 9000:9000 -p 9001:9001 \
  -e "MINIO_ROOT_USER=admin" \
  -e "MINIO_ROOT_PASSWORD=password123" \
  minio/minio server /data --console-address ":9001"

# Configure upload script for local testing
export S3_ENDPOINT="http://localhost:9000"
export AWS_ACCESS_KEY_ID="admin"
export AWS_SECRET_ACCESS_KEY="password123"
export S3_BUCKET="firmware"
```

Same upload script works with both AWS S3 and MinIO!

## Troubleshooting

### Issue: Access Denied
```bash
# Check bucket policy
aws s3api get-bucket-policy --bucket your-company-iot-firmware

# Check public access block
aws s3api get-public-access-block --bucket your-company-iot-firmware
```

### Issue: Bucket name already exists
```bash
# Bucket names are global - choose a unique name
aws s3 mb s3://your-company-name-iot-firmware-$(date +%s)
```

### Issue: Upload fails with credentials error
```bash
# Verify credentials
aws sts get-caller-identity

# Re-configure AWS CLI
aws configure
```
