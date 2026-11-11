/*
 * Smoke test: xác nhận compiler build được chương trình C
 * và CTest có thể chạy chương trình đó.
 *
 * Đây chưa phải test PID.
 */
int main(void)
{
    const float lhs = 0.25f;
    const float rhs = 0.25f;

    if ((lhs + rhs) != 0.5f) {
        return 1;
    }

    return 0;
}