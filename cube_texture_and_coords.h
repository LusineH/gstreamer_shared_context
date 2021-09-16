// Spatial coordinates for the cube

static const GLfloat quadx[6*4*3] = {
   /* FRONT */
   -1.f, -1.f,  1.f,
   1.f, -1.f,  1.f,
   -1.f,  1.f,  1.f,
   1.f,  1.f,  1.f,

   /* BACK */
   -1.f, -1.f, -1.f,
   -1.f,  1.f, -1.f,
   1.f, -1.f, -1.f,
   1.f,  1.f, -1.f,

   /* LEFT */
   -1.f, -1.f,  1.f,
   -1.f,  1.f,  1.f,
   -1.f, -1.f, -1.f,
   -1.f,  1.f, -1.f,

   /* RIGHT */
   1.f, -1.f, -1.f,
   1.f,  1.f, -1.f,
   1.f, -1.f,  1.f,
   1.f,  1.f,  1.f,

   /* TOP */
   -1.f,  1.f,  1.f,
   1.f,  1.f,  1.f,
   -1.f,  1.f, -1.f,
   1.f,  1.f, -1.f,

   /* BOTTOM */
   -1.f, -1.f,  1.f,
   -1.f, -1.f, -1.f,
   1.f, -1.f,  1.f,
   1.f, -1.f, -1.f,
};

/** Texture coordinates for the quad. */
static const GLfloat texCoords[6 * 4 * 2] = {
   0.f,  0.f,
   1.f,  0.f,
   0.f,  1.f,
   1.f,  1.f,

   0.f,  0.f,
   1.f,  0.f,
   0.f,  1.f,
   1.f,  1.f,

   0.f,  0.f,
   1.f,  0.f,
   0.f,  1.f,
   1.f,  1.f,

   0.f,  0.f,
   1.f,  0.f,
   0.f,  1.f,
   1.f,  1.f,

   0.f,  0.f,
   1.f,  0.f,
   0.f,  1.f,
   1.f,  1.f,

   0.f,  0.f,
   1.f,  0.f,
   0.f,  1.f,
   1.f,  1.f,
};



/* some helpers that we should provide in libgstgl */

typedef struct
{
  GLfloat m[4][4];
} GstGLMatrix;

static void
gst_gl_matrix_load_identity (GstGLMatrix * matrix)
{
  memset (matrix, 0x0, sizeof (GstGLMatrix));
  matrix->m[0][0] = 1.0f;
  matrix->m[1][1] = 1.0f;
  matrix->m[2][2] = 1.0f;
  matrix->m[3][3] = 1.0f;
}

static void
gst_gl_matrix_multiply (GstGLMatrix * matrix, GstGLMatrix * srcA,
    GstGLMatrix * srcB)
{
  GstGLMatrix tmp;
  int i;

  for (i = 0; i < 4; i++) {
    tmp.m[i][0] = (srcA->m[i][0] * srcB->m[0][0]) +
        (srcA->m[i][1] * srcB->m[1][0]) +
        (srcA->m[i][2] * srcB->m[2][0]) + (srcA->m[i][3] * srcB->m[3][0]);

    tmp.m[i][1] = (srcA->m[i][0] * srcB->m[0][1]) +
        (srcA->m[i][1] * srcB->m[1][1]) +
        (srcA->m[i][2] * srcB->m[2][1]) + (srcA->m[i][3] * srcB->m[3][1]);

    tmp.m[i][2] = (srcA->m[i][0] * srcB->m[0][2]) +
        (srcA->m[i][1] * srcB->m[1][2]) +
        (srcA->m[i][2] * srcB->m[2][2]) + (srcA->m[i][3] * srcB->m[3][2]);

    tmp.m[i][3] = (srcA->m[i][0] * srcB->m[0][3]) +
        (srcA->m[i][1] * srcB->m[1][3]) +
        (srcA->m[i][2] * srcB->m[2][3]) + (srcA->m[i][3] * srcB->m[3][3]);
  }

  memcpy (matrix, &tmp, sizeof (GstGLMatrix));
}

static void
gst_gl_matrix_translate (GstGLMatrix * matrix, GLfloat tx, GLfloat ty,
    GLfloat tz)
{
  matrix->m[3][0] +=
      (matrix->m[0][0] * tx + matrix->m[1][0] * ty + matrix->m[2][0] * tz);
  matrix->m[3][1] +=
      (matrix->m[0][1] * tx + matrix->m[1][1] * ty + matrix->m[2][1] * tz);
  matrix->m[3][2] +=
      (matrix->m[0][2] * tx + matrix->m[1][2] * ty + matrix->m[2][2] * tz);
  matrix->m[3][3] +=
      (matrix->m[0][3] * tx + matrix->m[1][3] * ty + matrix->m[2][3] * tz);
}

static void
gst_gl_matrix_frustum (GstGLMatrix * matrix, GLfloat left, GLfloat right,
    GLfloat bottom, GLfloat top, GLfloat nearZ, GLfloat farZ)
{
  GLfloat deltaX = right - left;
  GLfloat deltaY = top - bottom;
  GLfloat deltaZ = farZ - nearZ;
  GstGLMatrix frust;

  if ((nearZ <= 0.0f) || (farZ <= 0.0f) ||
      (deltaX <= 0.0f) || (deltaY <= 0.0f) || (deltaZ <= 0.0f))
    return;

  frust.m[0][0] = 2.0f * nearZ / deltaX;
  frust.m[0][1] = frust.m[0][2] = frust.m[0][3] = 0.0f;

  frust.m[1][1] = 2.0f * nearZ / deltaY;
  frust.m[1][0] = frust.m[1][2] = frust.m[1][3] = 0.0f;

  frust.m[2][0] = (right + left) / deltaX;
  frust.m[2][1] = (top + bottom) / deltaY;
  frust.m[2][2] = -(nearZ + farZ) / deltaZ;
  frust.m[2][3] = -1.0f;

  frust.m[3][2] = -2.0f * nearZ * farZ / deltaZ;
  frust.m[3][0] = frust.m[3][1] = frust.m[3][3] = 0.0f;

  gst_gl_matrix_multiply (matrix, &frust, matrix);
}

static void
gst_gl_matrix_perspective (GstGLMatrix * matrix, GLfloat fovy, GLfloat aspect,
    GLfloat nearZ, GLfloat farZ)
{
  GLfloat frustumW, frustumH;

  frustumH = tanf (fovy / 360.0f * M_PI) * nearZ;
  frustumW = frustumH * aspect;

  gst_gl_matrix_frustum (matrix, -frustumW, frustumW, -frustumH, frustumH,
      nearZ, farZ);
}
