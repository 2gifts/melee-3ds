"""Check that atlas boundary splitting preserves triangle coverage and shading."""
import numpy as np
from home_banner_atlas import split_clamped_triangle


def main():
    rng = np.random.default_rng(6724)
    count = 0
    for _ in range(200):
        # XY is an independent barycentric basis; remaining attributes vary.
        triangle = rng.uniform(-2,3,(3,11))
        triangle[:,:3] = [[0,0,0],[1,0,0],[0,1,0]]
        pieces = split_clamped_triangle(triangle)
        area = 0
        for piece in pieces:
            edge1, edge2 = piece[1,:2]-piece[0,:2],piece[2,:2]-piece[0,:2]
            a = edge1[0]*edge2[1]-edge1[1]*edge2[0]
            assert a >= -1e-10
            area += a
            barycentric = np.c_[1-piece[:,0]-piece[:,1],piece[:,:2]]
            assert np.allclose(piece,barycentric@triangle,atol=1e-10)
            for weights in ([1/3]*3,[.1,.2,.7],[.7,.2,.1]):
                weights = np.array(weights)
                # Atlas sampling must equal the old clamp-to-edge sampling
                # everywhere inside each new triangle, not just its vertices.
                assert np.allclose(weights@np.clip(piece[:,6:8],0,1),
                                   np.clip(weights@piece[:,6:8],0,1),atol=1e-10)
            count += 1
        assert np.isclose(area,1,atol=1e-10), 'Lost or duplicated triangle area'
    print(f'PASS: {count} atlas pieces preserve coverage, UV clamping and vertex attributes')


if __name__ == '__main__':
    main()
